#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include "CliToolsConfig.h"  // for CliPaths

#ifndef SPRAT_EMBEDDED_CLI
#include <QProcess>
#endif

class CliToolInstaller;
class QWidget;

/**
 * @class CliSetupController
 * @brief Manages CLI tool detection, installation, and binary path resolution.
 *
 * Extracted from MainWindow to satisfy Single Responsibility Principle.
 * Communicates results back to MainWindow via signals.
 */
class CliSetupController : public QObject {
    Q_OBJECT
public:
    // dialogParent is only used for parenting QMessageBox / QFileDialog dialogs
    explicit CliSetupController(QWidget* dialogParent, QObject* parent = nullptr);

    void check();
    void install();
    void setCliBaseDir(const QString& baseDir) { m_cliPaths.baseDir = baseDir; }

    /**
     * @brief Resolves binary paths quietly (no dialogs, no version check).
     * @return true if all binaries were found.
     * Emits binaryPathsResolved on success.
     */
    bool resolveQuietly();

    bool     isReady()          const { return m_cliReady; }
    CliPaths paths()            const { return m_cliPaths; }
    QString  layoutBinary()     const { return m_cliPaths.layoutBinary; }
    QString  framesBinary()     const { return m_cliPaths.framesBinary; }
    QString  convertBinary()    const { return m_cliPaths.convertBinary; }
    QString  packBinary()       const { return m_cliPaths.packBinary; }
    QString  unpackBinary()     const { return m_cliPaths.unpackBinary; }
    QString  buildDiagnosticsText(const QString& spritesFolder) const;

signals:
    void cliReady();
    void cliFailed();
    void binaryPathsResolved(CliPaths paths);
    void installStarted();
    void installProgress(qint64 received, qint64 total);
    void installLog(QString message);
    void installFinished(bool success);
    void statusMessageChanged(QString msg);
    void installOverlayShowNeeded();
    void installOverlayHideNeeded();

private slots:
#ifndef SPRAT_EMBEDDED_CLI
    void onInstallerFinished(int exitCode, QProcess::ExitStatus exitStatus);
#else
    void onInstallerFinished(int exitCode, int exitStatus);
#endif
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onInstallerLog(const QString& message);

private:
    bool resolveCliBinaries(QStringList& missing);
    void showMissingCliDialog(const QStringList& missing);
    void showCliExecutionError(const QString& tool);

    QWidget*          m_dialogParent  = nullptr;
    CliToolInstaller* m_installer     = nullptr;
    CliPaths          m_cliPaths;
    bool              m_cliReady      = false;
    bool              m_inCheck       = false;
    QString           m_installLogContent;
};
