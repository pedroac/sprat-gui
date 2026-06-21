#ifndef Q_OS_WASM

#include "UpdateChecker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static constexpr const char* kReleasesApiUrl =
    "https://api.github.com/repos/pedroac/sprat-gui/releases/latest";

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this)) {
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::check() {
    const QString current = QString(SPRAT_GUI_VERSION);
    if (current == QLatin1String("v0.0.0")) {
        return; // dev build — skip silently
    }

    QUrl url(kReleasesApiUrl);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_networkManager->get(request);
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit checkFailed("JSON parse error: " + parseError.errorString());
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tagName = obj[QLatin1String("tag_name")].toString();
    if (tagName.isEmpty()) {
        emit checkFailed("Missing tag_name in release response");
        return;
    }

    if (tagName == QString(SPRAT_GUI_VERSION)) {
        emit noUpdateAvailable();
        return;
    }

    // Pick the asset matching the current platform
    QString platformSuffix;
#if defined(Q_OS_LINUX)
    platformSuffix = QLatin1String("sprat-gui-linux.tar.gz");
#elif defined(Q_OS_WIN)
    platformSuffix = QLatin1String("sprat-gui-windows.zip");
#elif defined(Q_OS_MACOS)
    platformSuffix = QLatin1String("sprat-gui-macos.zip");
#endif

    if (platformSuffix.isEmpty()) {
        emit noUpdateAvailable();
        return;
    }

    QString downloadUrl;
    const QJsonArray assets = obj[QLatin1String("assets")].toArray();
    for (const QJsonValue& val : assets) {
        const QJsonObject asset = val.toObject();
        if (asset[QLatin1String("name")].toString() == platformSuffix) {
            downloadUrl = asset[QLatin1String("browser_download_url")].toString();
            break;
        }
    }

    if (downloadUrl.isEmpty()) {
        emit noUpdateAvailable();
        return;
    }

    ReleaseInfo info;
    info.version = tagName;
    info.releaseNotes = obj[QLatin1String("body")].toString();
    info.downloadUrl = downloadUrl;
    emit updateAvailable(info);
}

#endif // Q_OS_WASM
