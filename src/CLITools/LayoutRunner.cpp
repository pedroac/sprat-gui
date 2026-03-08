#include "LayoutRunner.h"
#include "ResolutionUtils.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QtConcurrent>

namespace {
    bool isCompactMode(const QString& mode) {
        return mode.trimmed().compare("compact", Qt::CaseInsensitive) == 0;
    }
}

LayoutRunner::LayoutRunner(QObject* parent) : QObject(parent), m_process(new QProcess(this)) {
    // We don't connect signals anymore as we'll run synchronously in a thread
}

LayoutRunner::~LayoutRunner() {
    stop();
}

void LayoutRunner::stop() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool LayoutRunner::isRunning() const {
    return m_process->state() != QProcess::NotRunning;
}

void LayoutRunner::run(const LayoutRunConfig& config) {
    if (isRunning()) {
        emit errorOccurred("Process is already running");
        return;
    }

    m_currentConfig = config;
    QStringList args = buildArguments(config);

    emit started();

    auto task = [this, config, args]() {
        QMutexLocker locker(m_mutex);
        
        m_stdoutBuffer.clear();
        m_stderrBuffer.clear();
        
        m_process->setProgram(config.layoutBinary);
        m_process->setArguments(args);
        m_process->start();

        if (!m_process->waitForStarted()) {
            emit errorOccurred("Failed to start process: " + config.layoutBinary);
            return;
        }

        QElapsedTimer timer;
        timer.start();
        const int timeoutMs = 300000; // 5 minutes for layout

        while (m_process->state() == QProcess::Running || m_process->bytesAvailable() > 0) {
            if (timer.elapsed() > timeoutMs) {
                m_process->kill();
                m_process->waitForFinished(1000);
                break;
            }

            m_process->waitForReadyRead(50);
            m_stdoutBuffer.append(m_process->readAllStandardOutput());
            m_stderrBuffer.append(m_process->readAllStandardError());
            
            if (m_process->state() == QProcess::NotRunning && m_process->bytesAvailable() == 0) break;
        }

        LayoutResult result;
        result.exitCode = m_process->exitCode();
        result.wasRetryingTrim = config.retryWithoutTrim;
        result.output = QString::fromUtf8(m_stdoutBuffer).trimmed();
        result.error = QString::fromUtf8(m_stderrBuffer).trimmed();

        if (m_process->exitStatus() == QProcess::CrashExit || result.exitCode != 0 || timer.elapsed() > timeoutMs) {
            result.success = false;
            if (timer.elapsed() > timeoutMs) {
                result.error = "Process timed out after 5 minutes.";
            } else if (m_process->exitStatus() == QProcess::CrashExit) {
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

        emit finished(result);
    };

    QtConcurrent::run(task);
}

QStringList LayoutRunner::buildArguments(const LayoutRunConfig& config) {
    QStringList args;
    args << config.sourcePath;

    const SpratProfile& p = config.profile;
    
    // Profile specific arguments
    if (!p.mode.trimmed().isEmpty()) {
        args << "--mode" << p.mode.trimmed();
    }
    if (!p.optimize.trimmed().isEmpty()) {
        args << "--optimize" << p.optimize.trimmed();
    }
    if (p.maxWidth > 0) {
        args << "--max-width" << QString::number(p.maxWidth);
    }
    if (p.maxHeight > 0) {
        args << "--max-height" << QString::number(p.maxHeight);
    }
    if (isCompactMode(p.mode) && p.maxCombinations > 0) {
        args << "--max-combinations" << QString::number(p.maxCombinations);
    }
    if (isCompactMode(p.mode) && p.threads > 0) {
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

    return args;
}
