#include "LayoutRunner.h"
#include "AppConstants.h"
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

LayoutRunner::LayoutRunner(QObject* parent) : QObject(parent)
{
}

LayoutRunner::~LayoutRunner() {
    stop();
#ifndef Q_OS_WASM
    m_future.waitForFinished();
#endif
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
    QStringList args = buildLayoutArguments(config);

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
        qInfo() << "[Embedded] spratlayout start"
                << "input=" << inputDesc
                << "args=" << args.join(' ');
        CliResult embeddedResult = EmbeddedCli::run("spratlayout", args, stdinPayload);
        qInfo() << "[Embedded] spratlayout done"
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
#ifndef SPRAT_EMBEDDED_CLI
        m_running.store(false);
#endif
        emit finished(result);
#endif
    };

#ifdef Q_OS_WASM
    // QtConcurrent/QThreadPool may not run without pthreads/COOP+COEP.
    task();
#else
#ifndef SPRAT_EMBEDDED_CLI
    m_running.store(true);
#endif
    m_future = QtConcurrent::run(task);
#endif
}
