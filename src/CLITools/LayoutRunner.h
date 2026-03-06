#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include "SpratProfilesConfig.h"

struct LayoutRunConfig {
    QString sourcePath;
    QString layoutBinary;
    SpratProfile profile;
    double scale = 1.0;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    bool retryWithoutTrim = false;
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

signals:
    void started();
    void finished(const LayoutResult& result);
    void errorOccurred(const QString& description);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    QProcess* m_process;
    LayoutRunConfig m_currentConfig;
    
    QStringList buildArguments(const LayoutRunConfig& config);
};
