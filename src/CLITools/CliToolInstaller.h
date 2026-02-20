#pragma once

#include <QObject>
#include <QStringList>
#include <QProcess>

/**
 * @class CliToolInstaller
 * @brief Manages installation and resolution of CLI tools.
 * 
 * This class handles the detection, installation, and management
 * of external CLI tools required by the application (spratlayout,
 * spratpack, and spratconvert).
 */
class CliToolInstaller : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructor for CliToolInstaller.
     * 
     * @param parent Parent QObject (optional)
     */
    explicit CliToolInstaller(QObject* parent = nullptr);

    /**
     * @brief Destructor for CliToolInstaller.
     */
    ~CliToolInstaller() override;

    /**
     * @brief Resolves CLI binaries and checks for missing ones.
     * 
     * This method checks if all required CLI tools are available
     * and returns a list of missing binaries.
     * 
     * @param missing Reference to store list of missing binaries
     * @return bool True if all binaries are found, false if some are missing
     */
    bool resolveCliBinaries(QStringList& missing);

    /**
     * @brief Installs missing CLI tools.
     * 
     * This method downloads and installs the required CLI tools
     * if they are not found on the system.
     */
    void installCliTools();

signals:
    /**
     * @brief Emitted when CLI tool installation finishes.
     * 
     * @param exitCode Exit code of the installation process
     * @param exitStatus Exit status of the installation process
     */
    void installFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Emitted when CLI tool installation starts.
     */
    void installStarted();

    /**
     * @brief Emitted when CLI tools resolution is complete.
     * 
     * @param ready True if all tools are ready, false otherwise
     */
    void cliToolsResolved(bool ready);

private slots:
    /**
     * @brief Handles completion of the installation process.
     * 
     * @param exitCode Exit code of the installation process
     * @param exitStatus Exit status of the installation process
     */
    void onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess* m_installProcess = nullptr; ///< Process for CLI tool installation
};
