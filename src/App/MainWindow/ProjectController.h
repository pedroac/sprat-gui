#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <QRect>
#include <QImage>
#include <QMutex>
#include <QTemporaryDir>
#include <atomic>
#include <memory>
#include <vector>

#include "models.h"
#include "DropAction.h"

class ProjectSession;
class QMimeData;

/**
 * @class ProjectController
 * @brief Controller for project loading, source management, ZIP/tar/image frame
 *        detection, and related async operations.
 *
 * Phase 3 of the SRP refactoring: this class owns the state (async watchers,
 * project-file path, pending-import state, network manager) that was previously
 * scattered across MainWindow.  MainWindow becomes a thin coordinator: it
 * connects to signals emitted here and performs the UI operations that cannot
 * be done from within the controller.
 */
class ProjectController : public QObject {
    Q_OBJECT

public:
    // Mirrors MainWindow::FrameDetectionResult
    struct FrameDetectionResult {
        QVector<QRect> frames;
        QColor         backgroundColor;
    };

    // FrameDetectionTaskResult is forwarded to MainWindow for dialog display
    struct FrameDetectionTaskResult {
        QString              imagePath;
        DropAction           action     = DropAction::Replace;
        FrameDetectionResult detection;
    };

    // Mirrors the internal structs in MainWindow for archive/frame extraction
    struct ProjectLoadResult {
        QString     path;
        QJsonObject root;
        QString     error;
        DropAction  action  = DropAction::Replace;
        bool        success = false;
    };

    struct ZipDiscoveryResult {
        QString    tempPath;
        QString    zipPath;
        QStringList selections;
        DropAction  action   = DropAction::Replace;
        bool        canceled = false;
        QString     error;
    };

    struct TarExtractionResult {
        QString   tempPath;
        QString   tarPath;
        DropAction action  = DropAction::Replace;
        bool       success = false;
    };

    struct FrameExtractionResult {
        QString   tempPath;
        QString   sourcePath;
        DropAction action        = DropAction::Replace;
        QColor     backgroundColor;
        bool       success       = false;
    };

    explicit ProjectController(ProjectSession* session, QObject* parent = nullptr);

    // ---- Binary paths (updated after CLI resolution) ----
    void setFramesBinary(const QString& p) { m_framesBinary = p; }
    void setConvertBinary(const QString& p) { m_convertBinary = p; }
    void setUnpackBinary(const QString& p)  { m_unpackBinary  = p; }
    QString framesBinary()  const { return m_framesBinary; }
    QString convertBinary() const { return m_convertBinary; }
    QString unpackBinary()  const { return m_unpackBinary; }

    // ---- Persistent project state ----
    QString projectFilePath() const           { return m_projectFilePath; }
    void    setProjectFilePath(const QString& p) { m_projectFilePath = p; }

    bool shouldClearSpritesFolder() const     { return m_shouldClearSpritesFolder; }
    void setShouldClearSpritesFolder(bool v)  { m_shouldClearSpritesFolder = v; }

    bool isSourceFolderTemp() const           { return m_sourceFolderIsTemp; }
    void setSourceFolderIsTemp(bool v)        { m_sourceFolderIsTemp = v; }

    bool mergeReplaceAllDuplicates() const    { return m_mergeReplaceAllDuplicates; }
    void setMergeReplaceAllDuplicates(bool v) { m_mergeReplaceAllDuplicates = v; }

    QString pendingImportUrl() const          { return m_pendingImportUrl; }
    void    setPendingImportUrl(const QString& u) { m_pendingImportUrl = u; }

    // ---- Cancellation flag (written by MainWindow::onCancelLoading) ----
    std::atomic<bool>& cancelFlag() { return m_isCanceled; }

    // ---- Cancel all in-flight operations ----
    void cancelAll();

    // ---- Watcher accessors for cancellation ----
    void cancelProjectLoadWatcher()     { m_projectLoadWatcher.cancel(); }
    void cancelZipDiscoveryWatcher()    { m_zipDiscoveryWatcher.cancel(); }
    void cancelFrameDetectionWatcher()  { m_frameDetectionWatcher.cancel(); }
    void cancelTarExtractionWatcher()   { m_tarExtractionWatcher.cancel(); }
    void cancelFrameExtractionWatcher() { m_frameExtractionWatcher.cancel(); }
    bool isProjectLoadRunning()         const { return m_projectLoadWatcher.isRunning(); }
    bool isZipDiscoveryRunning()        const { return m_zipDiscoveryWatcher.isRunning(); }
    bool isFrameDetectionRunning()      const { return m_frameDetectionWatcher.isRunning(); }
    bool isTarExtractionRunning()       const { return m_tarExtractionWatcher.isRunning(); }
    bool isFrameExtractionRunning()     const { return m_frameExtractionWatcher.isRunning(); }

    // ---- FutureWatcher results (called by MainWindow slot connections) ----
    ProjectLoadResult         projectLoadResult()     const { return m_projectLoadWatcher.result(); }
    ZipDiscoveryResult        zipDiscoveryResult()    const { return m_zipDiscoveryWatcher.result(); }
    FrameDetectionTaskResult  frameDetectionResult()  const { return m_frameDetectionWatcher.result(); }
    TarExtractionResult       tarExtractionResult()   const { return m_tarExtractionWatcher.result(); }
    FrameExtractionResult     frameExtractionResult() const { return m_frameExtractionWatcher.result(); }

    // ---- Network import ----
    QNetworkReply* activeImportReply() const  { return m_activeImportReply.data(); }
    void clearActiveImportReply()             { m_activeImportReply.clear(); }
    void setActiveImportReply(QNetworkReply* r) { m_activeImportReply = r; }

    // ---- Temp dirs for imports ----
    void addImportTempDir(std::unique_ptr<QTemporaryDir> d);
    void clearImportTempDirs();

    // ---- General temp dirs (moved from ProjectSession) ----
    void addTempDir(std::unique_ptr<QTemporaryDir> d);
    void clearTempDirs();
    void setSourceFolderTempDir(std::unique_ptr<QTemporaryDir> d);
    void clearSourceFolderTempDir();

    // ---- Async launch helpers ----
    // These mirror the MainWindow methods and schedule background work.
    // After finishing, the matching signal is emitted so MainWindow can call
    // processXxxResult() to do the UI-side work.
    void launchProjectLoad(const QString& path, DropAction action);
    void launchZipDiscovery(const QString& zipPath, DropAction action,
                            const QString& tempPath);
    void launchFrameDetection(const QString& imagePath, DropAction action);
    void launchTarExtraction(const QString& tarPath, DropAction action,
                             const QString& tempPath);
    void launchFrameExtraction(const QString& imagePath,
                               const QString& tempPath,
                               DropAction action,
                               const QColor& bgColor,
                               const QVector<QRect>& selectedFrames);

    // ---- Pure logic helpers that MainWindow currently owns ----
    FrameDetectionResult detectFramesInImage(const QString& imagePath) const;
    QString generateSpratFramesFormat(const QVector<QRect>& frames,
                                      const QString& imagePath) const;
    void applyTransparencyToImage(QImage& img, const QColor& bgColor) const;

    // ---- Source management helpers (shared with MainWindow) ----
    QString computeSourceSubfolderName(const QString& sourcePath) const;
    QString makeUniqueSourceName(const QString& baseName) const;
    QStringList copyFramesToSourceSubfolder(const QStringList& frames,
                                            const QString& subfolderPath,
                                            bool overwriteDuplicates = false) const;
    void syncFramePathsToNeutralAtlas(DropAction action);
    void registerLoadedSource(const QString& sourcePath,
                              DropAction action,
                              const QString& cachedFolderPath = QString());

    // ---- Network manager lazy init ----
    QNetworkAccessManager* importNetworkManager();

signals:
    // Emitted when an async operation starts a watcher run.
    void projectLoadStarted();
    void zipDiscoveryStarted();
    void frameDetectionStarted();
    void tarExtractionStarted();
    void frameExtractionStarted();

    // Emitted when a watcher finishes (MainWindow connects these to process* slots).
    void projectLoadFinished();
    void zipDiscoveryFinished();
    void frameDetectionFinished();
    void tarExtractionFinished();
    void frameExtractionFinished();

    // Emitted when all in-flight operations are canceled.
    void allCanceled();

    // Emitted from registerLoadedSource when a duplicate source triggers re-layout.
    void runLayoutQuietNeeded();

private:
    void onProjectLoadWatcherFinished();
    void onZipDiscoveryWatcherFinished();
    void onFrameDetectionWatcherFinished();
    void onTarExtractionWatcherFinished();
    void onFrameExtractionWatcherFinished();

    // ---- Internal source-management helpers (duplicate-free suffix of
    //      the code that was in the anonymous namespace of MainWindow.cpp) ----
    static QString sanitizeSubfolderNameStatic(const QString& name);

    ProjectSession* m_session = nullptr;

    // ---- Binary paths ----
    QString m_framesBinary;
    QString m_convertBinary;
    QString m_unpackBinary;

    // ---- Project state ----
    QString  m_projectFilePath;
    QString  m_pendingImportUrl;
    bool     m_shouldClearSpritesFolder = false;
    bool     m_sourceFolderIsTemp       = false;
    bool     m_mergeReplaceAllDuplicates = true;
    std::atomic<bool> m_isCanceled{false};

    // ---- Async watchers ----
    QFutureWatcher<ProjectLoadResult>        m_projectLoadWatcher;
    QFutureWatcher<ZipDiscoveryResult>       m_zipDiscoveryWatcher;
    QFutureWatcher<FrameDetectionTaskResult> m_frameDetectionWatcher;
    QFutureWatcher<TarExtractionResult>      m_tarExtractionWatcher;
    QFutureWatcher<FrameExtractionResult>    m_frameExtractionWatcher;

    // ---- Network import ----
    QNetworkAccessManager*  m_importNetworkManager = nullptr;
    QPointer<QNetworkReply> m_activeImportReply;
    std::vector<std::unique_ptr<QTemporaryDir>> m_importTempDirs;

    // ---- General temp dirs (moved from ProjectSession) ----
    std::vector<std::unique_ptr<QTemporaryDir>> m_tempDirs;
    std::unique_ptr<QTemporaryDir>              m_sourceFolderTempDir;
};
