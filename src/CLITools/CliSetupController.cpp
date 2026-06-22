#include "CliSetupController.h"
#include "CliToolInstaller.h"
#include "CliToolsConfig.h"
#include "CliToolsUi.h"
#include "SpratProfilesConfig.h"
#include "MessageDialog.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QSysInfo>
#include <QTimer>

#ifndef SPRAT_EMBEDDED_CLI
#include <QtConcurrent>
#endif

#ifdef Q_OS_WIN
#include <QCoreApplication>
#endif

CliSetupController::CliSetupController(QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , m_dialogParent(dialogParent)
{
    m_cliPaths = CliToolsConfig::loadCliPaths();

    m_installer = new CliToolInstaller(this);
    connect(m_installer, &CliToolInstaller::installFinished,
            this, &CliSetupController::onInstallerFinished);
    connect(m_installer, &CliToolInstaller::installStarted,
            this, &CliSetupController::installOverlayShowNeeded);
    connect(m_installer, &CliToolInstaller::downloadProgress,
            this, &CliSetupController::onDownloadProgress);
    connect(m_installer, &CliToolInstaller::installLog,
            this, &CliSetupController::onInstallerLog);

#ifndef SPRAT_EMBEDDED_CLI
    connect(&m_checkWatcher, &QFutureWatcher<CliVersionResult>::finished,
            this, &CliSetupController::onVersionCheckFinished);
#endif
}

void CliSetupController::check() {
    if (m_inCheck) return;
    m_inCheck = true;

    QStringList missing;
    bool allFound = resolveCliBinaries(missing);

    if (allFound) {
        emit binaryPathsResolved(m_cliPaths);

#ifndef SPRAT_EMBEDDED_CLI
        // Version checks are slow (up to 2 s each × 5 binaries).
        // Run them on a background thread; onVersionCheckFinished() resumes on
        // the main thread via QFutureWatcher when they complete.
        const CliPaths paths = m_cliPaths;  // capture by value for the lambda
        m_checkWatcher.setFuture(QtConcurrent::run([paths]() -> CliVersionResult {
            CliVersionResult r;
            r.layoutVersion = CliToolsConfig::checkBinaryVersion(paths.layoutBinary);
            if (!r.layoutVersion.isEmpty()) {
                r.packOk    = !CliToolsConfig::checkBinaryVersion(paths.packBinary).isEmpty();
                r.convertOk = !CliToolsConfig::checkBinaryVersion(paths.convertBinary).isEmpty();
                r.framesOk  = !CliToolsConfig::checkBinaryVersion(paths.framesBinary).isEmpty();
                r.unpackOk  = !CliToolsConfig::checkBinaryVersion(paths.unpackBinary).isEmpty();
            }
            // Pre-warm slow caches so ExportWorkspace::populate() and
            // updateCliDiagnostics() never have to block the main thread.
            CliToolsConfig::queryTransformsDir(paths.convertBinary);
            CliToolsConfig::queryDefaultProfilesConfig(paths.layoutBinary);
            return r;
        }));
        return;  // m_inCheck stays true until onVersionCheckFinished() clears it
#else
        // Embedded CLI: checks are instant (return compile-time constants).
        QString currentVersion = CliToolsConfig::checkBinaryVersion(m_cliPaths.layoutBinary);
        const QString requiredVersion = SPRAT_CLI_VERSION;

        if (currentVersion.isEmpty()) {
            m_cliReady = false;
            emit statusMessageChanged(tr("CLI error (failed to execute layout)"));
            showCliExecutionError("spratlayout");
            m_inCheck = false;
            emit cliFailed();
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.packBinary).isEmpty()) {
            showCliExecutionError("spratpack");
            m_inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.convertBinary).isEmpty()) {
            showCliExecutionError("spratconvert");
            m_inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.framesBinary).isEmpty()) {
            showCliExecutionError("spratframes");
            m_inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.unpackBinary).isEmpty()) {
            showCliExecutionError("spratunpack");
            m_inCheck = false;
            return;
        }
        if (currentVersion != requiredVersion) {
            if (CliToolsUi::askUpgrade(m_dialogParent, currentVersion, requiredVersion)) {
                m_cliReady = false;
                install();
                m_inCheck = false;
                return;
            }
            CliToolsConfig::saveInstalledCliVersion(currentVersion);
        }
        m_cliReady = true;
        CliToolsConfig::saveInstalledCliVersion(currentVersion);
        emit statusMessageChanged(tr("CLI ready (%1)").arg(currentVersion));
        emit cliReady();
#endif
    } else {
        m_cliReady = false;
        emit statusMessageChanged(tr("CLI missing"));
#ifdef Q_OS_WIN
        QDir appDir(QCoreApplication::applicationDirPath());
        if (!appDir.exists("cli")) {
            emit statusMessageChanged(tr("CLI folder missing"));
        }
#endif
        m_deferredCheckScheduled = false;
        showMissingCliDialog(missing);
        if (!m_deferredCheckScheduled)
            emit cliFailed();
        m_deferredCheckScheduled = false;
    }
    m_inCheck = false;
}

bool CliSetupController::resolveCliBinaries(QStringList& missing) {
#ifdef SPRAT_EMBEDDED_CLI
    m_cliPaths.layoutBinary  = "/spratlayout";
    m_cliPaths.packBinary    = "/spratpack";
    m_cliPaths.convertBinary = "/spratconvert";
    m_cliPaths.framesBinary  = "/spratframes";
    m_cliPaths.unpackBinary  = "/spratunpack";
    return true;
#else
    qInfo() << "[CliSetupController::resolveCliBinaries] baseDir=" << m_cliPaths.baseDir;
    m_cliPaths.layoutBinary = CliToolsConfig::resolveBinary("spratlayout", m_cliPaths.baseDir);
    if (m_cliPaths.layoutBinary.isEmpty()) {
        missing << "spratlayout";
    }

    m_cliPaths.packBinary = CliToolsConfig::resolveBinary("spratpack", m_cliPaths.baseDir);
    if (m_cliPaths.packBinary.isEmpty()) {
        missing << "spratpack";
    }

    m_cliPaths.convertBinary = CliToolsConfig::resolveBinary("spratconvert", m_cliPaths.baseDir);
    if (m_cliPaths.convertBinary.isEmpty()) {
        missing << "spratconvert";
    }

    m_cliPaths.framesBinary = CliToolsConfig::resolveBinary("spratframes", m_cliPaths.baseDir);
    if (m_cliPaths.framesBinary.isEmpty()) {
        missing << "spratframes";
    }

    m_cliPaths.unpackBinary = CliToolsConfig::resolveBinary("spratunpack", m_cliPaths.baseDir);
    if (m_cliPaths.unpackBinary.isEmpty()) {
        missing << "spratunpack";
    }

    return missing.isEmpty();
#endif
}

bool CliSetupController::resolveQuietly() {
    QStringList missing;
    bool allFound = resolveCliBinaries(missing);
    if (allFound) {
        emit binaryPathsResolved(m_cliPaths);
    }
    return allFound;
}

void CliSetupController::showMissingCliDialog(const QStringList& missing) {
    MissingCliAction action = CliToolsUi::askMissingCliAction(m_dialogParent, missing);
    if (action == MissingCliAction::Install) {
        install();
    } else if (action == MissingCliAction::ProvidePath) {
        QString dir = QFileDialog::getExistingDirectory(
            m_dialogParent, tr("Select CLI Tools Folder"), QDir::homePath());
        if (dir.isEmpty()) {
            emit statusMessageChanged(tr("CLI missing"));
            QApplication::quit();
            return;
        }
        m_cliPaths.baseDir = dir;
        CliToolsConfig::saveAppSettings(CliToolsConfig::loadAppSettings(), m_cliPaths);
        QStringList stillMissing;
        if (resolveCliBinaries(stillMissing)) {
            // A direct call to check() here is a no-op because m_inCheck is still
            // true (we are on the outer check()'s call stack).  Defer it so it
            // runs after the outer check() unwinds and resets m_inCheck.
            // Set m_deferredCheckScheduled so the outer check() suppresses the
            // spurious cliFailed() signal it would otherwise emit.
            m_deferredCheckScheduled = true;
            QTimer::singleShot(0, this, &CliSetupController::check);
        } else {
            MessageDialog::warning(m_dialogParent, tr("Tools Not Found"),
                tr("Some CLI tools were not found in the selected folder:\n%1")
                .arg(stillMissing.join(", ")));
            emit statusMessageChanged(tr("CLI missing"));
            QApplication::quit();
        }
    } else {
        emit statusMessageChanged(tr("CLI missing"));
        QApplication::quit();
    }
}

void CliSetupController::showCliExecutionError(const QString& tool) {
    m_cliReady = false;
    emit statusMessageChanged(tr("CLI error (failed to execute %1)").arg(tool));

    const auto pathFor = [this](const QString& name) -> QString {
        if (name == QLatin1String("spratlayout"))  return m_cliPaths.layoutBinary;
        if (name == QLatin1String("spratpack"))    return m_cliPaths.packBinary;
        if (name == QLatin1String("spratconvert")) return m_cliPaths.convertBinary;
        if (name == QLatin1String("spratframes"))  return m_cliPaths.framesBinary;
        if (name == QLatin1String("spratunpack"))  return m_cliPaths.unpackBinary;
        return {};
    };
    const QString path = pathFor(tool);

#if defined(Q_OS_WIN)
    const QString hint = tr("This is usually caused by missing runtime DLLs "
                            "(e.g. archive.dll, zlib1.dll) next to the executable.\n"
                            "Try reinstalling the CLI tools.");
#elif defined(Q_OS_LINUX)
    const QString hint = path.isEmpty()
        ? tr("Check that the binary has execute permission and all shared libraries are present.\n"
             "Try reinstalling the CLI tools.")
        : tr("Check for missing shared libraries:\n"
             "  ldd \"%1\"\n\n"
             "Verify execute permission:\n"
             "  chmod +x \"%1\"\n\n"
             "Try reinstalling the CLI tools if the problem persists.").arg(path);
#elif defined(Q_OS_MACOS)
    const QString hint = path.isEmpty()
        ? tr("The binary may be blocked by Gatekeeper or have missing dylib dependencies.\n"
             "Try reinstalling the CLI tools.")
        : tr("The binary may be blocked by Gatekeeper. To remove the quarantine flag, run:\n"
             "  xattr -d com.apple.quarantine \"%1\"\n\n"
             "Check for missing dylib dependencies:\n"
             "  otool -L \"%1\"\n\n"
             "Try reinstalling the CLI tools if the problem persists.").arg(path);
#else
    const QString hint = tr("Check that the binary has execute permission and all "
                            "required runtime dependencies are installed.\n"
                            "Try reinstalling the CLI tools.");
#endif

    const QString msg = path.isEmpty()
        ? tr("'%1' was found but could not be executed.\n\n%2").arg(tool, hint)
        : tr("'%1' was found at:\n  %2\n\nbut could not be executed.\n\n%3").arg(tool, path, hint);

    MessageDialog::critical(m_dialogParent, tr("CLI Execution Failed"), msg);
}

void CliSetupController::install() {
    if (!m_installer) {
        return;
    }
    m_installLogContent.clear();
    emit statusMessageChanged(tr("Installing CLI tools..."));
    m_installer->installCliTools();
}

#ifndef SPRAT_EMBEDDED_CLI
void CliSetupController::onVersionCheckFinished() {
    const CliVersionResult r = m_checkWatcher.result();
    const QString requiredVersion = SPRAT_CLI_VERSION;

    if (r.layoutVersion.isEmpty()) {
        m_cliReady = false;
        emit statusMessageChanged(tr("CLI error (failed to execute layout)"));
        showCliExecutionError("spratlayout");
        m_inCheck = false;
        emit cliFailed();
        return;
    }
    if (!r.packOk) {
        showCliExecutionError("spratpack");
        m_inCheck = false;
        return;
    }
    if (!r.convertOk) {
        showCliExecutionError("spratconvert");
        m_inCheck = false;
        return;
    }
    if (!r.framesOk) {
        showCliExecutionError("spratframes");
        m_inCheck = false;
        return;
    }
    if (!r.unpackOk) {
        showCliExecutionError("spratunpack");
        m_inCheck = false;
        return;
    }

    if (r.layoutVersion != requiredVersion) {
        if (CliToolsUi::askUpgrade(m_dialogParent, r.layoutVersion, requiredVersion)) {
            m_cliReady = false;
            install();
            m_inCheck = false;
            return;
        }
        // User declined upgrade; persist the version they chose to keep.
        CliToolsConfig::saveInstalledCliVersion(r.layoutVersion);
    }
    m_cliReady = true;
    CliToolsConfig::saveInstalledCliVersion(r.layoutVersion);
    emit statusMessageChanged(tr("CLI ready (%1)").arg(r.layoutVersion));
    emit cliReady();
    m_inCheck = false;
}
#endif

void CliSetupController::onInstallerFinished(int exitCode,
#ifndef SPRAT_EMBEDDED_CLI
    QProcess::ExitStatus exitStatus
#else
    int exitStatus
#endif
) {
    emit installOverlayHideNeeded();
#ifndef SPRAT_EMBEDDED_CLI
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
#else
    if (exitCode == 0) {
#endif
        emit statusMessageChanged(tr("CLI installation finished"));
        emit installFinished(true);
#ifndef SPRAT_EMBEDDED_CLI
        CliToolsConfig::invalidateCaches();  // ensure freshly installed binaries are re-queried
#endif
        check();
        return;
    }

    emit statusMessageChanged(tr("CLI installation failed"));
    emit installFinished(false);

    // Show error dialog with logs
    QMessageBox errorDialog(m_dialogParent);
    errorDialog.setWindowTitle(tr("CLI Installation Failed"));
    errorDialog.setIcon(QMessageBox::Critical);
    errorDialog.setText(tr("Could not install CLI tools automatically."));

    if (!m_installLogContent.isEmpty()) {
        errorDialog.setDetailedText(m_installLogContent);
    }

    errorDialog.exec();
}

void CliSetupController::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    emit installProgress(bytesReceived, bytesTotal);
}

void CliSetupController::onInstallerLog(const QString& message) {
    const QString trimmed = message.trimmed();
    if (!trimmed.isEmpty()) {
        if (!m_installLogContent.isEmpty()) {
            m_installLogContent += QLatin1Char('\n');
        }
        m_installLogContent += trimmed;
    }
    emit installLog(message);
}

QString CliSetupController::buildDiagnosticsText(const QString& spritesFolder) const {
    const QString ok    = QStringLiteral("  OK   ");
    const QString error = QStringLiteral("  ERROR");

    auto pathLine = [&](const QString& label, const QString& path) -> QString {
        const bool found = !path.isEmpty();
        const QString tag   = found ? ok : error;
        const QString key   = (label + QLatin1Char(':')).leftJustified(20);
        const QString value = found ? path : tr("not found");
        return tag + QLatin1Char(' ') + key + value + QLatin1Char('\n');
    };

    QString text;

    // System info
#if defined(Q_OS_WASM)
    const QString buildPlatform = QStringLiteral("WASM");
#elif defined(Q_OS_WIN)
    const QString buildPlatform = QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    const QString buildPlatform = QStringLiteral("macOS");
#elif defined(Q_OS_LINUX)
    const QString buildPlatform = QStringLiteral("Linux");
#else
    const QString buildPlatform = QStringLiteral("Unknown");
#endif
    text += QStringLiteral("GUI version:  %1  (%2)\n").arg(SPRAT_GUI_VERSION, buildPlatform);
    text += QStringLiteral("OS:           %1\n").arg(QSysInfo::prettyProductName());
    text += QStringLiteral("Kernel:       %1 %2\n").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
    text += QStringLiteral("CPU arch:     %1\n\n").arg(QSysInfo::currentCpuArchitecture());

    // CLI version
    const QString version = CliToolsConfig::checkBinaryVersion(m_cliPaths.layoutBinary);
    text += QStringLiteral("CLI version:  %1\n\n").arg(
        version.isEmpty() ? tr("ERROR  not found") : version);

    // Binaries
#ifdef SPRAT_EMBEDDED_CLI
    const QString embeddedTag = ok + QLatin1Char(' ');
    auto embeddedLine = [&](const QString& lbl) -> QString {
        return embeddedTag + (lbl + QLatin1Char(':')).leftJustified(20) + tr("embedded") + QLatin1Char('\n');
    };
    text += embeddedLine(QStringLiteral("spratlayout"));
    text += embeddedLine(QStringLiteral("spratpack"));
    text += embeddedLine(QStringLiteral("spratconvert"));
    text += embeddedLine(QStringLiteral("spratframes"));
    text += embeddedLine(QStringLiteral("spratunpack"));
#else
    text += pathLine(QStringLiteral("spratlayout"),   m_cliPaths.layoutBinary);
    text += pathLine(QStringLiteral("spratpack"),     m_cliPaths.packBinary);
    text += pathLine(QStringLiteral("spratconvert"),  m_cliPaths.convertBinary);
    text += pathLine(QStringLiteral("spratframes"),   m_cliPaths.framesBinary);
    text += pathLine(QStringLiteral("spratunpack"),   m_cliPaths.unpackBinary);
#endif

    text += QLatin1Char('\n');
    const QString profilesPath = SpratProfilesConfig::findProfilesConfigPath();
    text += pathLine(QStringLiteral("Profiles config"), profilesPath);

    const QString transformsDir = CliToolsConfig::queryTransformsDir(m_cliPaths.convertBinary);
    text += pathLine(QStringLiteral("Transforms dir"), transformsDir);

    text += QLatin1Char('\n');
    {
        const bool exists = !spritesFolder.isEmpty() && QDir(spritesFolder).exists();
        const QString tag = exists ? ok : error;
        const QString key = QStringLiteral("Sprites folder:").leftJustified(20);
        const QString value = spritesFolder.isEmpty() ? tr("not loaded")
                            : exists                  ? spritesFolder
                                                      : spritesFolder + tr(" (not found)");
        text += tag + QLatin1Char(' ') + key + value + QLatin1Char('\n');
    }
    return text;
}
