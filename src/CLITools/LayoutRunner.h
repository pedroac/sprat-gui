#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QMutex>
#include <QElapsedTimer>
#include "SpratProfilesConfig.h"

struct LayoutRunConfig {
    QString sourcePath;
    QString layoutBinary;
    SpratProfile profile;
    double scale = 1.0;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    bool retryWithoutTrim = false;
    QString deduplicateMode = "none";
};

struct LayoutResult {
    bool success;
    int exitCode;
    QString output;
    QString error;
    bool wasRetryingTrim;
};

class LayoutRunner : public QObject {
    Q_OBJECT
public:
    explicit LayoutRunner(QObject* parent = nullptr);
    ~LayoutRunner() override;

    void run(const LayoutRunConfig& config);
    void stop();
    bool isRunning() const;

    void setMutex(QMutex* mutex) { m_mutex = mutex; }

signals:
    void started();
    void finished(const LayoutResult& result);
    void errorOccurred(const QString& description);
    void logMessage(const QString& text);

private:
#ifndef SPRAT_EMBEDDED_CLI
    QProcess* m_process;
#endif
    QMutex* m_mutex = nullptr;
    LayoutRunConfig m_currentConfig;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    
    QStringList buildArguments(const LayoutRunConfig& config);
};
