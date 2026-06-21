#include "LayoutRunner.h"
#include "AppConstants.h"
#include "ResolutionUtils.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QProcess>
#include <QThreadPool>
#include <QtConcurrent>
#include <QTime>
#include <QFile>

#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

namespace {
    bool isCompactPreset(const QString& preset) {
        const QString p = preset.trimmed().toLower();
        return p == "quality" || p == "small";
    }
}

LayoutRunner::LayoutRunner(QObject* parent) : QObject(parent)
{
}

LayoutRunner::~LayoutRunner() {
    stop();
}

void LayoutRunner::stop() {
#ifdef SPRAT_EMBEDDED_CLI
    EmbeddedCli::requestStop();
#else
    m_stopRequested = true;
#endif
}

bool LayoutRunner::isRunning() const {
#ifdef SPRAT_EMBEDDED_CLI
    return false;
#else
    return m_running.load();
#endif
}

void LayoutRunner::run(const LayoutRunConfig& config) {
    if (isRunning()) {
        emit errorOccurred("Process is already running");
        return;
    }

    m_currentConfig = config;
    QStringList args = buildArguments(config);

    emit started();

    // Build stdin payload for --stdin-list mode
    QByteArray stdinPayload;
    if (!config.imagePathList.isEmpty()) {
        stdinPayload = (config.imagePathList.join('\n') + '\n').toUtf8();
    }

    auto task = [this, config, args, stdinPayload]() {
        QMutexLocker locker(m_mutex);

        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();

#ifndef SPRAT_EMBEDDED_CLI
        m_stopRequested = false;
#endif

        QElapsedTimer runTimer;
        runTimer.start();

#ifdef SPRAT_EMBEDDED_CLI
        Q_UNUSED(config.layoutBinary);
        QElapsedTimer timer;
        timer.start();
        const QString inputDesc = config.imagePathList.isEmpty()
            ? config.sourceFolderPath
            : QStringLiteral("--stdin-list (%1 paths)").arg(config.imagePathList.size());
        qInfo() << "[WASM] spratlayout start"
                << "input=" << inputDesc
                << "args=" << args.join(' ');
        CliResult embeddedResult = EmbeddedCli::run("spratlayout", args, stdinPayload);
        qInfo() << "[WASM] spratlayout done"
                << "exit=" << embeddedResult.exitCode
                << "stdoutBytes=" << embeddedResult.stdOut.size()
                << "stderrBytes=" << embeddedResult.stdErr.size()
                << "ms=" << timer.elapsed();
        
        LayoutResult result;
        result.exitCode = embeddedResult.exitCode;
        result.wasRetryingTrim = config.retryWithoutTrim;
        result.output = QString::fromUtf8(embeddedResult.stdOut).trimmed();
        result.error = QString::fromUtf8(embeddedResult.stdErr).trimmed();
        result.success = (result.exitCode == 0);
#else
        // QProcess is created here in the thread-pool thread so its children
        // (socket notifiers, pipes) are also created in the same thread, avoiding
        // the "Cannot create children for a parent that is in a different thread" warning.
        QProcess process;
        process.setProgram(config.layoutBinary);
        process.setArguments(args);
        process.start();

        if (!process.waitForStarted()) {
            m_running.store(false);
            emit errorOccurred("Failed to start process: " + config.layoutBinary);
            return;
        }

        // For --stdin-list mode: write image paths to process stdin then close the channel.
        if (!stdinPayload.isEmpty()) {
            process.write(stdinPayload);
            process.closeWriteChannel();
        }

        QElapsedTimer timer;
        timer.start();
        const int timeoutMs = AppConstants::kLayoutProcessTimeoutMs;

        while (process.state() == QProcess::Running || process.bytesAvailable() > 0) {
            if (m_stopRequested.load()) {
                process.kill();
                process.waitForFinished(500);
                LayoutResult killed;
                killed.success = false;
                killed.exitCode = -1;
                killed.wasRetryingTrim = config.retryWithoutTrim;
                killed.wasKilledIntentionally = true;
                m_running.store(false);
                emit finished(killed);
                return;
            }

            if (timer.elapsed() > timeoutMs) {
                process.kill();
                process.waitForFinished(1000);
                break;
            }

            process.waitForReadyRead(50);
            m_stdoutBuffer.append(process.readAllStandardOutput());
            m_stderrBuffer.append(process.readAllStandardError());

            if (process.state() == QProcess::NotRunning && process.bytesAvailable() == 0) break;
        }

        LayoutResult result;
        result.exitCode = process.exitCode();
        result.wasRetryingTrim = config.retryWithoutTrim;
        result.output = QString::fromUtf8(m_stdoutBuffer).trimmed();
        result.error = QString::fromUtf8(m_stderrBuffer).trimmed();

        if (process.exitStatus() == QProcess::CrashExit || result.exitCode != 0 || timer.elapsed() > timeoutMs) {
            result.success = false;
            if (timer.elapsed() > timeoutMs) {
                result.error = "Process timed out after 5 minutes.";
            } else if (process.exitStatus() == QProcess::CrashExit) {
#ifdef Q_OS_WIN
                if (!result.error.isEmpty()) result.error += "\n\n";
                result.error += "Process crashed. This might be due to missing dependencies like 'archive.dll'. Please check the 'cli' folder.";
#else
                result.error = "Process crashed.";
#endif
            }
        } else {
            result.success = true;
        }
#endif

        // Emit CLI log
        {
            QString binary = config.layoutBinary.isEmpty() ? QStringLiteral("spratlayout") : QFileInfo(config.layoutBinary).fileName();
            qint64 ms = runTimer.elapsed();
            QString logEntry = QStringLiteral("[%1] %2 %3\n[%1] Exit: %4 (%5 ms)")
                .arg(QTime::currentTime().toString("HH:mm:ss"), binary, args.join(' '),
                     QString::number(result.exitCode), QString::number(ms));
            if (!result.error.isEmpty()) {
                logEntry += QStringLiteral("\n  stderr: %1").arg(result.error);
            }
            // Log input details
            if (!config.imagePathList.isEmpty()) {
                logEntry += QStringLiteral("\n  input: --stdin-list (%1 paths)").arg(config.imagePathList.size());
            } else if (!config.sourceFolderPath.isEmpty()) {
                QFileInfo inputInfo(config.sourceFolderPath);
                logEntry += QStringLiteral("\n  input: %1 (isDir=%2, exists=%3)")
                    .arg(config.sourceFolderPath,
                         inputInfo.isDir() ? "yes" : "no",
                         inputInfo.exists() ? "yes" : "no");
            }
            emit logMessage(logEntry);
        }

#ifdef Q_OS_WASM
        QMetaObject::invokeMethod(this, [this, result]() {
            emit finished(result);
        }, Qt::AutoConnection);
        // Qt::AutoConnection: task() runs on the main thread (same as this), so Qt
        // resolves AutoConnection as DirectConnection and emits finished() immediately.
        // Qt::QueuedConnection would post a QMetaCallEvent that the WASM backend
        // won't process (no requestAnimationFrame requested) until the next user
        // input event, causing onLayoutFinished() — and the "Loading images..."
        // overlay removal — to stall until cursor movement.
#else
        m_running.store(false);
        emit finished(result);
#endif
    };

#ifdef Q_OS_WASM
    // QtConcurrent/QThreadPool may not run without pthreads/COOP+COEP.
    task();
#else
    m_running.store(true);
    QThreadPool::globalInstance()->start(task);
#endif
}

QStringList LayoutRunner::buildArguments(const LayoutRunConfig& config) {
    QStringList args;
    if (!config.imagePathList.isEmpty()) {
        args << "--stdin-list";
    } else if (!config.sourceFolderPath.isEmpty()) {
        args << config.sourceFolderPath;
    }

    const SpratProfile& p = config.profile;
    
    // Profile specific arguments
    if (!p.preset.trimmed().isEmpty()) {
        args << "--preset" << p.preset.trimmed();
    }
    if (p.maxWidth > 0) {
        args << "--max-width" << QString::number(p.maxWidth);
    }
    if (p.maxHeight > 0) {
        args << "--max-height" << QString::number(p.maxHeight);
    }
    if (isCompactPreset(p.preset) && p.threads > 0) {
        args << "--threads" << QString::number(p.threads);
    }

    // Scale
    double effectiveScale = config.scale;
    if (std::abs(effectiveScale - 1.0) < 1e-6 && p.scale > 0) {
        effectiveScale = p.scale;
    }
    args << "--scale" << QString::number(effectiveScale, 'g', 12);

    // Resolution
    const bool hasSourceResolution = config.sourceResolutionWidth > 0 && config.sourceResolutionHeight > 0;
    const bool hasTargetResolution = p.targetResolutionUseSource || 
                                     (p.targetResolutionWidth > 0 && p.targetResolutionHeight > 0);

    if (hasSourceResolution && hasTargetResolution) {
        const int targetW = p.targetResolutionUseSource ? config.sourceResolutionWidth : p.targetResolutionWidth;
        const int targetH = p.targetResolutionUseSource ? config.sourceResolutionHeight : p.targetResolutionHeight;
        
        args << "--source-resolution" << formatResolutionText(config.sourceResolutionWidth, config.sourceResolutionHeight);
        args << "--target-resolution" << formatResolutionText(targetW, targetH);
        
        if (!p.resolutionReference.trimmed().isEmpty()) {
            args << "--resolution-reference" << p.resolutionReference.trimmed();
        }
    }

    // Padding & Extrude & Trim & Rotate
    args << "--padding" << QString::number(p.padding);
    if (p.extrude > 0) {
        args << "--extrude" << QString::number(p.extrude);
    }
    
    if (p.trimTransparent && !config.retryWithoutTrim) {
        args << "--trim-transparent";
    }
    if (p.allowRotation) {
        args << "--rotate";
    }
    if (p.multipack) {
        args << "--multipack";
    }
    if (!p.sort.trimmed().isEmpty()) {
        args << "--sort" << p.sort.trimmed();
    }

    if (!config.deduplicateMode.isEmpty() && config.deduplicateMode != "none") {
        args << "--deduplicate" << config.deduplicateMode;
    }

    return args;
}
