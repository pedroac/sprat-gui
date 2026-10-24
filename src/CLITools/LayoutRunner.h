#pragma once

#include <QObject>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include "LayoutArguments.h"

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
};
