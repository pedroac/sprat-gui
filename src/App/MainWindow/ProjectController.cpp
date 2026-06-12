#include "ProjectController.h"
#include "ProjectSession.h"
#include "ProjectFileLoader.h"
#include "ArchiveExtractor.h"
#include "ImageDiscoveryService.h"
#include "models.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#ifndef Q_OS_WASM
#include <QProcess>
#endif
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtConcurrent>

// ---------------------------------------------------------------------------
// Constructor / watcher wiring
// ---------------------------------------------------------------------------
ProjectController::ProjectController(ProjectSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_projectLoadWatcher,    &QFutureWatcher<ProjectLoadResult>::finished,
            this, &ProjectController::onProjectLoadWatcherFinished);
    connect(&m_zipDiscoveryWatcher,   &QFutureWatcher<ZipDiscoveryResult>::finished,
            this, &ProjectController::onZipDiscoveryWatcherFinished);
    connect(&m_frameDetectionWatcher, &QFutureWatcher<FrameDetectionTaskResult>::finished,
            this, &ProjectController::onFrameDetectionWatcherFinished);
    connect(&m_tarExtractionWatcher,  &QFutureWatcher<TarExtractionResult>::finished,
            this, &ProjectController::onTarExtractionWatcherFinished);
    connect(&m_frameExtractionWatcher,&QFutureWatcher<FrameExtractionResult>::finished,
            this, &ProjectController::onFrameExtractionWatcherFinished);
}

// ---------------------------------------------------------------------------
// cancelAll
// ---------------------------------------------------------------------------
void ProjectController::cancelAll()
{
    m_isCanceled = true;
    if (m_activeImportReply) m_activeImportReply->abort();
    m_projectLoadWatcher.cancel();
    m_zipDiscoveryWatcher.cancel();
    m_frameDetectionWatcher.cancel();
    m_tarExtractionWatcher.cancel();
    m_frameExtractionWatcher.cancel();
    emit allCanceled();
}

// ---------------------------------------------------------------------------
// Temp dir management
// ---------------------------------------------------------------------------
void ProjectController::addImportTempDir(std::unique_ptr<QTemporaryDir> d)
{
    m_importTempDirs.push_back(std::move(d));
}

void ProjectController::clearImportTempDirs()
{
    m_importTempDirs.clear();
}

void ProjectController::addTempDir(std::unique_ptr<QTemporaryDir> d)
{
    m_tempDirs.push_back(std::move(d));
}

void ProjectController::clearTempDirs()
{
    m_tempDirs.clear();
}

void ProjectController::setSourceFolderTempDir(std::unique_ptr<QTemporaryDir> d)
{
    m_sourceFolderTempDir = std::move(d);
}

void ProjectController::clearSourceFolderTempDir()
{
    m_sourceFolderTempDir.reset();
}

// ---------------------------------------------------------------------------
// Network manager lazy init
// ---------------------------------------------------------------------------
QNetworkAccessManager* ProjectController::importNetworkManager()
{
    if (!m_importNetworkManager)
        m_importNetworkManager = new QNetworkAccessManager(this);
    return m_importNetworkManager;
}

// ---------------------------------------------------------------------------
// Async launch helpers
// ---------------------------------------------------------------------------
void ProjectController::launchProjectLoad(const QString& path, DropAction action)
{
    if (m_projectLoadWatcher.isRunning()) return;
    auto task = [path, action]() {
        ProjectLoadResult result;
        result.path   = path;
        result.action = action;
        result.success = ProjectFileLoader::load(path, result.root, result.error);
        return result;
    };
    m_projectLoadWatcher.setFuture(QtConcurrent::run(task));
    emit projectLoadStarted();
}

void ProjectController::launchZipDiscovery(const QString& zipPath,
                                           DropAction action,
                                           const QString& tempPath)
{
    if (m_zipDiscoveryWatcher.isRunning()) return;
    auto task = [this, zipPath, action, tempPath]() {
        ZipDiscoveryResult result;
        result.tempPath = tempPath;
        result.zipPath  = zipPath;
        result.action   = action;
        result.canceled = false;
        QString error;
        if (ArchiveExtractor::extractToDirectory(zipPath, tempPath, error, &m_isCanceled)) {
            if (m_isCanceled)
                result.canceled = true;
            else
                result.selections = ImageDiscoveryService::imageDirectoriesRecursive(tempPath);
        } else {
            if (m_isCanceled)
                result.canceled = true;
            else {
                result.error = error;
                qWarning() << "ArchiveExtractor error:" << error;
            }
        }
        return result;
    };
    m_zipDiscoveryWatcher.setFuture(QtConcurrent::run(task));
    emit zipDiscoveryStarted();
}

void ProjectController::launchFrameDetection(const QString& imagePath, DropAction action)
{
    if (m_frameDetectionWatcher.isRunning()) return;
    auto task = [this, imagePath, action]() {
        FrameDetectionTaskResult result;
        result.imagePath = imagePath;
        result.action    = action;
        result.detection = detectFramesInImage(imagePath);
        return result;
    };
    m_frameDetectionWatcher.setFuture(QtConcurrent::run(task));
    emit frameDetectionStarted();
}

void ProjectController::launchTarExtraction(const QString& tarPath,
                                            DropAction action,
                                            const QString& tempPath)
{
    if (m_tarExtractionWatcher.isRunning()) return;
    auto task = [this, tarPath, action, tempPath]() {
        TarExtractionResult result;
        result.tempPath = tempPath;
        result.tarPath  = tarPath;
        result.action   = action;
        result.success  = false;
        QString error;
        if (ArchiveExtractor::extractToDirectory(tarPath, tempPath, error, &m_isCanceled))
            result.success = !m_isCanceled.load();
        else
            qWarning() << "ArchiveExtractor (tar) error:" << error;
        return result;
    };
    m_tarExtractionWatcher.setFuture(QtConcurrent::run(task));
    emit tarExtractionStarted();
}

void ProjectController::launchFrameExtraction(const QString& imagePath,
                                              const QString& tempPath,
                                              DropAction action,
                                              const QColor& bgColor,
                                              const QVector<QRect>& selectedFrames)
{
    if (m_frameExtractionWatcher.isRunning()) return;
    const QString unpackBin = m_unpackBinary;
    auto task = [this, imagePath, tempPath, action, bgColor, selectedFrames, unpackBin]() {
        FrameExtractionResult res;
        res.tempPath       = tempPath;
        res.sourcePath     = imagePath;
        res.action         = action;
        res.backgroundColor = bgColor;

        QString framesData = generateSpratFramesFormat(selectedFrames, imagePath);
        QStringList args;
        args << imagePath << "--frames" << "-" << "--output" << tempPath;
        QByteArray framesDataBytes = framesData.toUtf8();

#ifndef Q_OS_WASM
        QProcess proc;
        proc.start(unpackBin, args);
        if (!framesDataBytes.isEmpty()) {
            proc.write(framesDataBytes);
            proc.closeWriteChannel();
        }
        res.success = proc.waitForFinished(60000) &&
                      proc.exitStatus() == QProcess::NormalExit &&
                      proc.exitCode() == 0;
#endif
        return res;
    };
    m_frameExtractionWatcher.setFuture(QtConcurrent::run(task));
    emit frameExtractionStarted();
}

// ---------------------------------------------------------------------------
// Watcher finished relay slots
// ---------------------------------------------------------------------------
void ProjectController::onProjectLoadWatcherFinished()    { emit projectLoadFinished(); }
void ProjectController::onZipDiscoveryWatcherFinished()   { emit zipDiscoveryFinished(); }
void ProjectController::onFrameDetectionWatcherFinished() { emit frameDetectionFinished(); }
void ProjectController::onTarExtractionWatcherFinished()  { emit tarExtractionFinished(); }
void ProjectController::onFrameExtractionWatcherFinished(){ emit frameExtractionFinished(); }

// ---------------------------------------------------------------------------
// detectFramesInImage
// Runs spratframes in a subprocess.  Called from a background thread.
// ---------------------------------------------------------------------------
ProjectController::FrameDetectionResult
ProjectController::detectFramesInImage(const QString& imagePath) const
{
    FrameDetectionResult result;
    if (m_framesBinary.isEmpty()) return result;

#ifdef Q_OS_WASM
    return result;
#else
    QProcess proc;
    proc.start(m_framesBinary, QStringList() << imagePath);
    if (!proc.waitForFinished(30000)) {
        qWarning() << "spratframes timed out";
        return result;
    }
    if (proc.exitCode() != 0) {
        qWarning() << "spratframes error";
        return result;
    }
    const QByteArray output = proc.readAllStandardOutput();
    QString outputStr = QString::fromUtf8(output);
    QTextStream stream(&outputStr);
    QString line;
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith("path ")) continue;

        if (line.startsWith("background")) {
            QString bgColorStr = line.mid(10).trimmed();
            if (bgColorStr.startsWith(":")) bgColorStr = bgColorStr.mid(1).trimmed();
            if (bgColorStr.startsWith("#") ||
                (bgColorStr.length() == 6 &&
                 QRegularExpression("^[0-9a-fA-F]{6}$").match(bgColorStr).hasMatch())) {
                QString hexStr = bgColorStr.startsWith("#") ? bgColorStr : "#" + bgColorStr;
                result.backgroundColor = QColor(hexStr);
                if (result.backgroundColor.isValid()) continue;
            }
            QStringList parts = bgColorStr.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                bool rOk, gOk, bOk;
                int r = parts[0].toInt(&rOk), g = parts[1].toInt(&gOk), b = parts[2].toInt(&bOk);
                if (rOk && gOk && bOk) result.backgroundColor = QColor(r, g, b);
            }
            continue;
        }

        if (line.startsWith("sprite ")) {
            QString spriteData = line.mid(7).trimmed();
            QStringList parts = spriteData.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                QStringList posParts  = parts[0].split(',');
                QStringList sizeParts = parts[1].split(',');
                if (posParts.size() == 2 && sizeParts.size() == 2) {
                    bool ok = true;
                    int x = posParts[0].toInt(&ok); if (!ok) continue;
                    int y = posParts[1].toInt(&ok); if (!ok) continue;
                    int w = sizeParts[0].toInt(&ok); if (!ok) continue;
                    int h = sizeParts[1].toInt(&ok); if (!ok) continue;
                    result.frames.append(QRect(x, y, w, h));
                }
            }
        }
    }
    return result;
#endif // Q_OS_WASM
}

// ---------------------------------------------------------------------------
// generateSpratFramesFormat
// ---------------------------------------------------------------------------
QString ProjectController::generateSpratFramesFormat(const QVector<QRect>& frames,
                                                      const QString& imagePath) const
{
    QString format;
    QTextStream stream(&format);
    stream << "path \"" << imagePath << "\"\n";
    for (const QRect& f : frames)
        stream << "sprite " << f.x() << "," << f.y() << " "
               << f.width() << "," << f.height() << "\n";
    return format;
}

// ---------------------------------------------------------------------------
// applyTransparencyToImage
// ---------------------------------------------------------------------------
void ProjectController::applyTransparencyToImage(QImage& img, const QColor& bgColor) const
{
    if (!bgColor.isValid()) return;
    const QRgb target = bgColor.rgb();
    const int tr = qRed(target), tg = qGreen(target), tb = qBlue(target);
    const int tolerance = 15;
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* scanLine = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb pixel = scanLine[x];
            if (qAbs(qRed(pixel) - tr) <= tolerance &&
                qAbs(qGreen(pixel) - tg) <= tolerance &&
                qAbs(qBlue(pixel) - tb) <= tolerance)
                scanLine[x] = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// sanitizeSubfolderNameStatic  (local helper)
// ---------------------------------------------------------------------------
QString ProjectController::sanitizeSubfolderNameStatic(const QString& name)
{
    QString result;
    result.reserve(name.size());
    for (const QChar& c : name) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
                || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
                || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|'))
            result += QLatin1Char('_');
        else
            result += c;
    }
    return result.isEmpty() ? QStringLiteral("source") : result;
}

// ---------------------------------------------------------------------------
// computeSourceSubfolderName
// ---------------------------------------------------------------------------
QString ProjectController::computeSourceSubfolderName(const QString& sourcePath) const
{
    QString name;
    if (!m_pendingImportUrl.isEmpty()) {
        const QUrl url(m_pendingImportUrl);
        name = QFileInfo(url.path()).fileName();
        if (name.isEmpty()) name = url.host();
        if (name.isEmpty()) name = QStringLiteral("url-import");
        name = QFileInfo(name).baseName();
    } else {
        name = QFileInfo(sourcePath).baseName();
    }
    name = sanitizeSubfolderNameStatic(name);

    if (m_session && !m_session->sourceFolder.isEmpty()) {
        auto isUsed = [this](const QString& n) {
            return QDir(QDir(m_session->sourceFolder).filePath(n)).exists();
        };
        if (isUsed(name)) {
            int i = 2;
            QString candidate;
            do {
                candidate = name + QLatin1Char('_') + QString::number(i++);
            } while (isUsed(candidate) && i < 100);
            name = candidate;
        }
    }
    return name;
}

// ---------------------------------------------------------------------------
// makeUniqueSourceName
// ---------------------------------------------------------------------------
QString ProjectController::makeUniqueSourceName(const QString& baseName) const
{
    if (!m_session) return baseName;
    auto isUsed = [this](const QString& n) {
        for (const ProjectSource& s : m_session->sources)
            if (s.name == n) return true;
        return false;
    };
    if (!isUsed(baseName)) return baseName;
    int i = 2;
    QString candidate;
    do {
        candidate = baseName + QLatin1Char('_') + QString::number(i++);
    } while (isUsed(candidate) && i < 100);
    return candidate;
}

// ---------------------------------------------------------------------------
// copyFramesToSourceSubfolder
// ---------------------------------------------------------------------------
QStringList ProjectController::copyFramesToSourceSubfolder(const QStringList& frames,
                                                            const QString& subfolderPath,
                                                            bool overwriteDuplicates) const
{
    QDir dstDir(subfolderPath);
    dstDir.mkpath(QStringLiteral("."));

    const QString originalRootPath = m_session ? m_session->currentFolder : QString();
    QDir originalRootDir(originalRootPath);
    QStringList result;
    result.reserve(frames.size());

    for (const QString& path : frames) {
        const QFileInfo srcInfo(path);
        const QString canonicalSrc = srcInfo.canonicalFilePath();
        const QString absPath = canonicalSrc.isEmpty() ? srcInfo.absoluteFilePath() : canonicalSrc;

        const QString canonicalDst = QDir(subfolderPath).canonicalPath();
        if (!canonicalDst.isEmpty() && absPath.startsWith(canonicalDst + QLatin1Char('/'))) {
            result.append(absPath);
            continue;
        }

        QString relPath;
        if (!originalRootPath.isEmpty() && absPath.startsWith(originalRootPath))
            relPath = originalRootDir.relativeFilePath(absPath);
        else
            relPath = srcInfo.fileName();

        QString dst = dstDir.filePath(relPath);
        const QFileInfo dstInfo(dst);
        const QString dstDirPath = dstInfo.absolutePath();
        if (!QDir(dstDirPath).exists()) QDir().mkpath(dstDirPath);

        if (dstInfo.exists()) {
            if (overwriteDuplicates) {
                QFile::remove(dst);
            } else {
                const QString baseName = dstInfo.completeBaseName();
                const QString suffix   = dstInfo.suffix();
                bool resolved = false;
                for (int i = 1; i <= 99; ++i) {
                    const QString candidate = QDir(dstDirPath).filePath(
                        QStringLiteral("%1_%2.%3").arg(baseName).arg(i).arg(suffix));
                    if (!QFileInfo::exists(candidate)) { dst = candidate; resolved = true; break; }
                }
                if (!resolved) { result.append(absPath); continue; }
            }
        }

        if (QFile::copy(absPath, dst))
            result.append(dst);
        else {
            qWarning() << "copyFramesToSourceSubfolder: copy failed" << absPath << "->" << dst;
            result.append(absPath);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// syncFramePathsToNeutralAtlas
// ---------------------------------------------------------------------------
void ProjectController::syncFramePathsToNeutralAtlas(DropAction action)
{
    if (!m_session || m_session->atlases.isEmpty()) return;
    const int neutralIdx = m_session->neutralAtlasIndex();
    AtlasEntry& neutral = m_session->atlases[neutralIdx];
    if (action == DropAction::Replace) neutral.spritePaths.clear();
    for (const QString& path : m_session->activeFramePaths) {
        const QString norm = QFileInfo(path).absoluteFilePath();
        if (!m_session->atlasForSprite(norm))
            neutral.spritePaths.append(norm);
    }
}

// ---------------------------------------------------------------------------
// registerLoadedSource
// ---------------------------------------------------------------------------
void ProjectController::registerLoadedSource(const QString& sourcePath,
                                              DropAction action,
                                              const QString& cachedFolderPath)
{
    if (!m_session) return;

    ProjectSource src;
    if (!m_pendingImportUrl.isEmpty()) {
        const QUrl url(m_pendingImportUrl);
        QString baseName = QFileInfo(url.path()).fileName();
        if (baseName.isEmpty()) baseName = url.host();
        if (baseName.isEmpty()) baseName = m_pendingImportUrl;
        src.name = makeUniqueSourceName(baseName);
        src.type = SourceType::Url;
        src.originalPath = m_pendingImportUrl;
        m_pendingImportUrl.clear();
    } else {
        src.name = makeUniqueSourceName(QFileInfo(sourcePath).fileName());
        const QString lower = sourcePath.toLower();
        src.type = (lower.endsWith(".zip") || lower.endsWith(".tar")
                    || lower.endsWith(".tar.gz") || lower.endsWith(".tgz")
                    || lower.endsWith(".tar.bz2") || lower.endsWith(".tar.xz"))
                   ? SourceType::Archive : SourceType::SingleImage;
        src.originalPath = sourcePath;
    }

    src.cachedFolderPath = !cachedFolderPath.isEmpty()
        ? cachedFolderPath
        : (action == DropAction::Replace ? m_session->sourceFolder : QString());

    if (action == DropAction::Replace) {
        m_session->sources.clear();
    } else {
        for (const ProjectSource& existing : m_session->sources) {
            if (existing.originalPath == src.originalPath) {
                if (!src.cachedFolderPath.isEmpty()) {
                    const QString cleanedCache = QDir::cleanPath(src.cachedFolderPath);
                    m_session->activeFramePaths.erase(
                        std::remove_if(m_session->activeFramePaths.begin(),
                                       m_session->activeFramePaths.end(),
                                       [&cleanedCache](const QString& p) {
                                           const QString cp = QDir::cleanPath(p);
                                           return cp.startsWith(cleanedCache + QLatin1Char('/'))
                                               || cp == cleanedCache;
                                       }),
                        m_session->activeFramePaths.end());
                    QDir(src.cachedFolderPath).removeRecursively();
                }
                emit runLayoutQuietNeeded();
                return;
            }
        }
    }
    m_session->sources.append(src);
    syncFramePathsToNeutralAtlas(action);
}
