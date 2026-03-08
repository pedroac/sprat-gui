#include "LayoutRunner.h"
#include "ResolutionUtils.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace {
    bool isCompactMode(const QString& mode) {
        return mode.trimmed().compare("compact", Qt::CaseInsensitive) == 0;
    }
}

LayoutRunner::LayoutRunner(QObject* parent) : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LayoutRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &LayoutRunner::onProcessError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer.append(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrBuffer.append(m_process->readAllStandardError());
    });
}

LayoutRunner::~LayoutRunner() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished();
    }
}

void LayoutRunner::stop() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
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

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_currentConfig = config;
    QStringList args = buildArguments(config);

    emit started();
    m_process->start(config.layoutBinary, args);
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

void LayoutRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // Final drain
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    m_stderrBuffer.append(m_process->readAllStandardError());

    LayoutResult result;
    result.exitCode = exitCode;
    result.wasRetryingTrim = m_currentConfig.retryWithoutTrim;
    result.output = QString::fromUtf8(m_stdoutBuffer).trimmed();
    result.error = QString::fromUtf8(m_stderrBuffer).trimmed();

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        result.success = false;
#ifdef Q_OS_WIN
        if (exitStatus == QProcess::CrashExit) {
            if (!result.error.isEmpty()) result.error += "\n\n";
            result.error += "Process crashed. This might be due to missing dependencies like 'archive.dll'. Please check the 'cli' folder.";
        }
#endif
    } else {
        result.success = true;
    }

    emit finished(result);
}

void LayoutRunner::onProcessError(QProcess::ProcessError error) {
    QString errStr = m_process->errorString();
    if (error == QProcess::FailedToStart) {
        errStr = "Failed to start process: " + m_currentConfig.layoutBinary;
#ifdef Q_OS_WIN
        errStr += "\n\nThis might be due to missing dependencies like 'archive.dll'. Please ensure all required DLLs are in the 'cli' folder.";
#endif
    }
    emit errorOccurred(errStr);
}
