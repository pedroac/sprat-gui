#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QMap>
#include <QStringList>
#include <QPointer>
#include <QVariantAnimation>
#include <functional>
#include <atomic>
#include "SyncMode.h"
#include "ViewEnums.h"
#include "LayoutModels.h"
#include "LayoutRunner.h"

class ILayoutContext;
class ProjectSession;
class LayoutCanvas;
class QComboBox;
class AtlasesManagementWorkspace;
class QStackedWidget;

/**
 * @class LayoutOrchestrator
 * @brief Manages all layout run scheduling, debouncing, sprite animation, and profile fallback.
 *
 * Extracted from MainWindow to satisfy Single Responsibility Principle.
 * Communicates with MainWindow exclusively via signals.
 */
class LayoutOrchestrator : public QObject {
    Q_OBJECT
public:
    struct Config {
        ProjectSession*              session         = nullptr;
        LayoutCanvas*                canvas          = nullptr;
        QComboBox*                   profileCombo    = nullptr;
        QComboBox*                   resolutionCombo = nullptr;
        QStackedWidget*              atlasViewStack  = nullptr;
        AtlasesManagementWorkspace*  atlasMgmtWorkspace = nullptr;
        QString                      layoutBinary;
        ILayoutContext*              context         = nullptr;
    };

    explicit LayoutOrchestrator(const Config& cfg, QObject* parent = nullptr);

    // --- Settings setters ---
    void setSyncMode(SyncMode mode);
    void setDeduplicateMode(const QString& mode);
    void setLayoutZoomOnChange(LayoutZoomOnChange mode);
    void setEnableAnimation(bool v);
    void setCLIReady(bool ready);
    void setMergeReplaceAllDuplicates(bool v);
    void setActiveWorkspace(int ws);
    void setAtlasMgmtWorkspace(AtlasesManagementWorkspace* w);

    // --- State setters ---
    void setRetryWithoutTrimOnFailure(bool v);
    void markCenterPivotsOnNextLayout();
    void setDirty(bool v);
    void updateLayoutBinary(const QString& path);
    void clearProfilesTried();
    void setCurrentProfile(const QString& profile);

    // --- Public interface ---
    void schedule(bool immediate = false, bool skipCapture = false);
    void pause();
    void resume();
    void capturePositions();
    void run(bool quiet = false);
    void stop();
    void resetDebounceTimer(); // Restarts debounce timer if it was running (called on user interaction)
    void stopAndClearPending(); // Stops the runner and clears any pending run

    bool isDirty() const { return m_layoutDirty; }
    int  layoutGeneration() const { return m_layoutGeneration; }

signals:
    void layoutFinished(QVector<LayoutModel> models, QStringList selectedPaths, QString primaryPath);
    void layoutFailed(QString error);
    void layoutAppliedToCanvas();
    void layoutRunStarted(bool quiet);
    void loadingStateChanged(bool loading);
    void statusMessageChanged(QString msg);
    void profileFallbackRequested(QString newProfile);
    void profileDisableRequested(QString profile);
    void spriteTreeRefreshNeeded();
    void activeFrameListUpdateNeeded();
    void uiUpdateNeeded();
    void openSourceFolderActionUpdateNeeded();
    void cliDiagnosticsUpdateNeeded();
    void layoutZoomResetRequested(bool profileChanged);
    void atlasDimsUpdated(QString text);
    void pendingProjectPayloadReady();
    void selectSpritesByPathsRequested(QStringList paths, QString primary);
    void cliReadyCheckNeeded();
    void tempDirsCleanupNeeded();
    void manualFrameLabelUpdateNeeded();
    void mainContentViewUpdateNeeded();
    void initSourceFolderWatcherNeeded();

private slots:
    void onRunnerFinished(const LayoutResult& result);
    void onRunnerError(const QString& description);

private:
    LayoutRunConfig buildConfig() const;
    void handleProfileFailure(const QString& failedProfile);
    void handleDimensionsError(const QString& failedProfile);
    bool isProfileEnabled(const QString& profile) const;
    void animateToNewPositions(const QMap<QString, QPointF>& newPositions,
                               const QVector<QRectF>& newAtlasRects,
                               const QVector<LayoutModel>& newModels,
                               std::function<void()> onFinished);

    Config m_cfg;

    LayoutRunner*               m_layoutRunner         = nullptr;
    QTimer*                     m_layoutDebounceTimer  = nullptr;
    int                         m_layoutGeneration     = 0;
    bool                        m_layoutRunPending     = false;
    bool                        m_layoutRunPendingQuiet = false;
    bool                        m_layoutDirty          = false;
    bool                        m_layoutRebuildPaused  = false;
    QMap<QString, QPointF>      m_oldSpritePositions;
    QMap<QString, QRect>        m_oldSpritePackedRects;
    QMap<QString, bool>         m_oldSpriteRotated;
    bool                        m_enableSpriteAnimation = true;
    QPointer<QVariantAnimation> m_spriteAnimation;
    QStringList                 m_spriteAnimationPaths;
    int                         m_pendingChangeCount   = 0;
    QString                     m_runningLayoutProfile;
    bool                        m_retryWithoutTrimOnFailure  = false;
    bool                        m_layoutFailureDialogShown   = false;
    QStringList                 m_profilesTriedForCurrentLoad;
    bool                        m_centerPivotsOnNextLayout   = false;
    QString                     m_currentProfile;
    QString                     m_currentResolution;

    SyncMode             m_syncMode           = SyncMode::Watch;
    QString              m_deduplicateMode    = "none";
    LayoutZoomOnChange   m_layoutZoomOnChange = LayoutZoomOnChange::NoChange;
    bool                 m_cliReady           = false;
    bool                 m_mergeReplaceAllDuplicates = true;
    int                  m_activeWorkspace    = 0;

    std::atomic<bool>    m_isCanceled{false};
};
