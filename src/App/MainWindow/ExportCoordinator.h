#pragma once
#include <QObject>
#include <QTimer>
#include <QFutureWatcher>
#include <QVector>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <atomic>
#include <memory>
#include "models.h"

class QWidget;
class ProjectSession;
class LayoutCanvas;
class PackedAtlasView;
class ExportWorkspace;
class LayoutOrchestrator;
class ILayoutContext;

/**
 * @class ExportCoordinator
 * @brief Owns the export pipeline and preview pack async state.
 *
 * Extracted from MainWindow to satisfy Single Responsibility Principle.
 * Communicates with MainWindow exclusively via signals.
 */
class ExportCoordinator : public QObject {
    Q_OBJECT
public:
    struct ExportResult {
        QString savedDestination;
        QString error;
        bool    success  = false;
        bool    canceled = false;
        QVector<ExportLogEntry> logEntries;
    };

    struct PackPreviewResult {
        QByteArray           imageData;
        QString              errorMsg;
        QByteArray           layoutUsed;
        QString              scaleFilterUsed;
        int                  dilateUsed = -1;
        QVector<LayoutModel> layoutModels;
    };

    struct Config {
        QWidget*            parentWidget       = nullptr;
        ProjectSession*     session            = nullptr;
        LayoutOrchestrator* layoutOrchestrator = nullptr;
        LayoutCanvas*       exportLayoutCanvas = nullptr;
        PackedAtlasView*    packedAtlasView    = nullptr;
        ExportWorkspace*    exportWorkspace    = nullptr;
        ILayoutContext*     layoutContext      = nullptr;

        // runTool: matches MainWindow::runTool signature
        std::function<bool(const QString&, const QStringList&,
                           const QByteArray*, QByteArray*, QByteArray*)> runTool;
        // buildProjectPayload: wraps MainWindow::buildProjectPayload(config, session, portable)
        std::function<QJsonObject(const SaveConfig&, bool)> buildProjectPayload;
        // promoteSourceFolderAfterSave: post-export hook (stays in MainWindow)
        std::function<void(const QString&)> promoteSourceFolderAfterSave;
    };

    explicit ExportCoordinator(const Config& cfg, QObject* parent = nullptr);
    ~ExportCoordinator() override;

    // --- Settings ---
    void setAppSettings(const AppSettings& settings);
    void setCliReady(bool ready);
    void setLayoutBinary(const QString& path);
    void setPackBinary(const QString& path);
    void setConvertBinary(const QString& path);
    void setExportWorkspaceActive(bool active);
    void setExportPreviewAtlasIndex(int idx);
    void invalidatePreviewCache();

    // --- Actions ---
    bool runExport(SaveConfig config);
    void refreshPreview(const QString& profileName, const QString& scaleFilter);
    void schedulePreviewPack(const QString& profileName, const QString& scaleFilter);
    void cancelPreview();
    void cancelExport();

    bool isExportRunning() const;
    bool isPreviewRunning() const;

signals:
    void statusChanged(QString msg);
    void loadingStateChanged(bool loading);
    void exportLogReady(QVector<ExportLogEntry> entries, QString destination);

private slots:
    void onExportWatcherFinished();
    void onPreviewPackWatcherFinished();
    void doRunPreviewPack();

private:
    void handleExportResult(const ExportResult& result);
    void handlePackPreviewResult(const PackPreviewResult& result);
    void emitStatus(const QString& msg);

    Config      m_cfg;
    AppSettings m_settings;
    bool        m_cliReady              = false;
    bool        m_exportWorkspaceActive = false;
    QString     m_layoutBinary;
    QString     m_packBinary;
    QString     m_convertBinary;
    int         m_exportPreviewAtlasIndex = -1;

    QFutureWatcher<ExportResult>       m_exportWatcher;
    QFutureWatcher<PackPreviewResult>  m_previewPackWatcher;
    QTimer*                            m_previewPackDebounceTimer = nullptr;
    std::shared_ptr<std::atomic<bool>> m_previewPackCanceled{std::make_shared<std::atomic<bool>>(false)};
    std::atomic<bool>                  m_exportCanceled{false};
    QString                            m_previewPackProfile;
    QString                            m_previewPackScaleFilter;
    QByteArray                         m_cachedPackedImage;
    QByteArray                         m_cachedPackLayout;
    QString                            m_cachedPackScaleFilter;
    int                                m_cachedPackDilate = -1;
    std::shared_ptr<std::atomic<bool>> m_previewPackLayoutUpdateCanceled;
    QVector<LayoutModel>               m_cachedPackModels;
    QString                            m_cachedPackModelsProfile;
};
