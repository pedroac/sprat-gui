#pragma once

#include <QObject>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include "SpratProfilesConfig.h"

struct LayoutRunConfig {
    // Provide exactly one of the two input fields:
    QString sourceFolderPath; // Pass folder as a positional argument (fast path)
    QStringList imagePathList; // Pass image list via --stdin-list
    QString layoutBinary;
    SpratProfile profile;
    double scale = 1.0;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    bool retryWithoutTrim = false;
    QString deduplicateMode = "none";
    int dedupThreshold = 5;
    bool incrementalLayout = false;
};

struct LayoutResult {
    bool success;
    int exitCode;
    QString output;
    QString error;
    bool wasRetryingTrim;
    bool wasKilledIntentionally = false;
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
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};
#endif
#ifndef Q_OS_WASM
    QFuture<void> m_future;
#endif
    QMutex* m_mutex = nullptr;
    LayoutRunConfig m_currentConfig;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    
    QStringList buildArguments(const LayoutRunConfig& config);
};
