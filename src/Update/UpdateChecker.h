#pragma once
#ifndef Q_OS_WASM

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct ReleaseInfo {
        QString version;
        QString releaseNotes;
        QString downloadUrl;
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    // Non-blocking: fires one of the three signals when done.
    void check();

    /// True when candidate is a later version than current, comparing "vX.Y.Z" numerically.
    /// Unparsable versions are never newer, so a malformed tag can't trigger an update.
    static bool isNewerVersion(const QString& candidate, const QString& current);

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
