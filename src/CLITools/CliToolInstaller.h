#pragma once

#include <QObject>
#include <QStringList>
#include <QProcess>

class CliToolInstaller : public QObject {
    Q_OBJECT
public:
    CliToolInstaller(QObject* parent = nullptr);
    ~CliToolInstaller() override;

    bool resolveCliBinaries(QStringList& missing);
    void installCliTools();

signals:
    void installFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void installStarted();
    void cliToolsResolved(bool ready);

private slots:
    void onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess* m_installProcess = nullptr;
};