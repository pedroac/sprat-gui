#pragma once
#ifndef Q_OS_WASM

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct ReleaseInfo {
        QString version;
        QString releaseNotes;
        QString downloadUrl;
        QString sha256; ///< Lower-case hex SHA-256 of the download, from GitHub's asset digest
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    // Non-blocking: fires one of the three signals when done.
    void check();

    /// True when candidate is a later version than current, comparing "vX.Y.Z" numerically.
    /// Unparsable versions are never newer, so a malformed tag can't trigger an update.
    static bool isNewerVersion(const QString& candidate, const QString& current);

    /// Name of the release asset this build updates from: the AppImage when running as one
    /// (Linux), otherwise the platform archive. Empty when the platform has no updater.
    static QString platformAssetName();

    /// Finds the asset named assetName in a GitHub release's "assets" array.
    /// @return False unless it has a download URL and a "sha256:<64 hex>" digest
    static bool findAsset(const QJsonArray& assets, const QString& assetName,
                          QString& downloadUrl, QString& sha256);

signals:
    void updateAvailable(const UpdateChecker::ReleaseInfo& info);
    void noUpdateAvailable();
    void checkFailed(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_networkManager;
};

#endif // Q_OS_WASM
