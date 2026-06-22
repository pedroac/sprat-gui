#include "LayoutOrchestrator.h"
#include "ILayoutContext.h"
#include "LayoutCanvas.h"
#include "AtlasesManagementWorkspace.h"
#include "ProjectSession.h"
#include "LayoutRunner.h"
#include "LayoutParser.h"
#include "ResolutionUtils.h"
#include "SpriteNameUtils.h"
#include "AppConstants.h"
#include "AnimationPreviewService.h"
#include "MessageDialog.h"
#include "SpratProfilesConfig.h"

#include <QComboBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>

LayoutOrchestrator::LayoutOrchestrator(const Config& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg)
{
    m_layoutRunner = new LayoutRunner(this);
    connect(m_layoutRunner, &LayoutRunner::finished,
            this, &LayoutOrchestrator::onRunnerFinished);
    connect(m_layoutRunner, &LayoutRunner::errorOccurred,
            this, &LayoutOrchestrator::onRunnerError);
    connect(m_layoutRunner, &LayoutRunner::logMessage,
            this, &LayoutOrchestrator::logMessage);
}

// --- Settings setters ---

void LayoutOrchestrator::setSyncMode(SyncMode mode) { m_syncMode = mode; }
void LayoutOrchestrator::setDeduplicateMode(const QString& mode) { m_deduplicateMode = mode; }
void LayoutOrchestrator::setLayoutZoomOnChange(LayoutZoomOnChange mode) { m_layoutZoomOnChange = mode; }
void LayoutOrchestrator::setEnableAnimation(bool v) { m_enableSpriteAnimation = v; }
void LayoutOrchestrator::setCLIReady(bool ready) { m_cliReady = ready; }
void LayoutOrchestrator::setMergeReplaceAllDuplicates(bool v) { m_mergeReplaceAllDuplicates = v; }
void LayoutOrchestrator::setActiveWorkspace(int ws) { m_activeWorkspace = ws; }
void LayoutOrchestrator::setAtlasMgmtWorkspace(AtlasesManagementWorkspace* w) { m_cfg.atlasMgmtWorkspace = w; }

// --- State setters ---

void LayoutOrchestrator::setRetryWithoutTrimOnFailure(bool v) { m_retryWithoutTrimOnFailure = v; }
void LayoutOrchestrator::markCenterPivotsOnNextLayout() { m_centerPivotsOnNextLayout = true; }
void LayoutOrchestrator::setDirty(bool v) { m_layoutDirty = v; }
void LayoutOrchestrator::updateLayoutBinary(const QString& path) { m_cfg.layoutBinary = path; }
void LayoutOrchestrator::clearProfilesTried() { m_profilesTriedForCurrentLoad.clear(); }
void LayoutOrchestrator::setCurrentProfile(const QString& profile) { m_currentProfile = profile; }

// --- stop ---

void LayoutOrchestrator::stop() {
    if (m_layoutRunner) m_layoutRunner->stop();
}

void LayoutOrchestrator::resetDebounceTimer() {
    if (m_layoutDebounceTimer && m_layoutDebounceTimer->isActive()) {
        m_layoutDebounceTimer->start(m_layoutDebounceTimer->interval());
    }
}

void LayoutOrchestrator::stopAndClearPending() {
    if (m_layoutRunner) m_layoutRunner->stop();
    m_layoutRunPending = false;
    m_layoutRunPendingQuiet = false;
}

// --- pause / resume ---

void LayoutOrchestrator::pause() {
    if (m_layoutDebounceTimer && m_layoutDebounceTimer->isActive()) {
        m_layoutDebounceTimer->stop();
        m_layoutRebuildPaused = true;
    }
}

void LayoutOrchestrator::resume() {
    if (m_layoutRebuildPaused && m_layoutDirty && !m_layoutRunner) {
        schedule();
    }
    m_layoutRebuildPaused = false;
}

// --- capturePositions ---

void LayoutOrchestrator::capturePositions() {
    m_oldSpritePositions.clear();
    m_oldSpritePackedRects.clear();
    m_oldSpriteRotated.clear();
    if (!m_cfg.canvas) return;

    for (const auto& model : m_cfg.session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite) continue;
            auto pos = m_cfg.canvas->spriteItemScenePos(sprite->path);
            if (pos.has_value()) {
                m_oldSpritePositions[sprite->path]   = pos.value();
                m_oldSpritePackedRects[sprite->path] = sprite->rect;
                m_oldSpriteRotated[sprite->path]     = sprite->rotated;
            }
        }
    }
}

// --- schedule ---

void LayoutOrchestrator::schedule(bool immediate, bool skipCapture) {
    if (!skipCapture) {
        capturePositions();
    }

    ++m_pendingChangeCount;
    emit spriteTreeRefreshNeeded();

    const bool wasRunning = m_layoutRunner && m_layoutRunner->isRunning();
    if (wasRunning) {
        m_layoutRunner->stop();
        m_layoutRunPending = false;
        m_layoutRunPendingQuiet = false;
    }

    const bool bufferFull = (m_pendingChangeCount >= AppConstants::kLayoutBufferFullThreshold);

    if (bufferFull) {
        m_pendingChangeCount = 0;
        if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
        run(true);
    } else {
#ifdef Q_OS_WASM
        if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
        m_layoutDirty = false;
        emit statusMessageChanged(tr("Layout pending rebuild..."));
        run(true);
#else
        if (immediate) {
            if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
            m_layoutDirty = false;
            emit statusMessageChanged(tr("Layout pending rebuild..."));
            run(true);
        } else {
            if (!m_layoutDebounceTimer) {
                m_layoutDebounceTimer = new QTimer(this);
                m_layoutDebounceTimer->setSingleShot(true);
                connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
                    if (m_cfg.atlasViewStack && m_cfg.atlasViewStack->currentIndex() == 1) {
                        m_layoutDirty = true;
                    } else {
                        m_layoutDirty = false;
                        run(true);
                    }
                });
            }
            if (!m_layoutRebuildPaused) {
                m_layoutDebounceTimer->start(AppConstants::kLayoutDebounceMs);
            } else {
                m_layoutDirty = true;
            }
            emit statusMessageChanged(tr("Layout pending rebuild..."));
        }
#endif
    }
}

// --- buildConfig ---

LayoutRunConfig LayoutOrchestrator::buildConfig() const {
    LayoutRunConfig cfg;

    const bool atlasesLayoutMode = m_activeWorkspace == 3  // Workspace::AtlasesManagement
        && m_cfg.atlasMgmtWorkspace
        && m_cfg.atlasMgmtWorkspace->viewMode() == AtlasesManagementWorkspace::ViewMode::Layout;
    if (atlasesLayoutMode) {
        cfg.imagePathList = m_cfg.session->activeAtlas().spritePaths;
        return cfg;
    }

    if (!m_cfg.session->sources.isEmpty()) {
        const bool singleFolderOptimised =
            m_cfg.session->sources.size() == 1
            && m_cfg.session->sources.first().type == SourceType::Folder
            && m_syncMode != SyncMode::None
            && !m_cfg.session->sourceFolder.isEmpty()
            && m_cfg.context
            && m_cfg.context->sourceFolderMatchesActiveFrames();
        if (singleFolderOptimised) {
            cfg.sourceFolderPath = m_cfg.session->sourceFolder;
        } else if (!m_cfg.session->activeFramePaths.isEmpty()) {
            cfg.imagePathList = m_cfg.session->activeFramePaths;
        } else if (!m_cfg.session->layoutSourcePath.isEmpty()) {
            cfg.sourceFolderPath = m_cfg.session->layoutSourcePath;
        }
        return cfg;
    }

    if (m_syncMode != SyncMode::None
            && m_cfg.context
            && m_cfg.context->sourceFolderMatchesActiveFrames()) {
        cfg.sourceFolderPath = m_cfg.session->sourceFolder;
    } else if (m_cfg.session->layoutSourceIsList && !m_cfg.session->activeFramePaths.isEmpty()) {
        cfg.imagePathList = m_cfg.session->activeFramePaths;
    } else if (!m_cfg.session->layoutSourcePath.isEmpty()) {
        cfg.sourceFolderPath = m_cfg.session->layoutSourcePath;
    }
    return cfg;
}

// --- run ---

void LayoutOrchestrator::run(bool quiet) {
    QElapsedTimer totalTimer;
    totalTimer.start();

    if (m_syncMode != SyncMode::None
        && m_cfg.session
        && !m_cfg.session->sourceFolder.isEmpty()
        && !m_cfg.session->activeFramePaths.isEmpty()
        && m_cfg.context
        && !m_cfg.context->activeFramesAreInSourceFolder()) {

        QElapsedTimer syncTimer;
        syncTimer.start();
        if (m_cfg.context)
            m_cfg.context->copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        qInfo() << "[Performance] LayoutOrchestrator::run sync/copy took" << syncTimer.elapsed() << "ms";
    }

    const LayoutRunConfig inputCfg = buildConfig();
    if (inputCfg.sourceFolderPath.isEmpty() && inputCfg.imagePathList.isEmpty()) {
        const bool atlasesLayoutMode = m_activeWorkspace == 3
            && m_cfg.atlasMgmtWorkspace
            && m_cfg.atlasMgmtWorkspace->viewMode() == AtlasesManagementWorkspace::ViewMode::Layout;
        if (atlasesLayoutMode) {
            m_cfg.session->activeAtlas().layoutModels.clear();
            AnimationPreviewService::invalidateSpriteMap();
            emit activeFrameListUpdateNeeded();
            m_cfg.canvas->setModels({});
            emit statusMessageChanged(tr("Empty atlas"));
            emit mainContentViewUpdateNeeded();
            emit uiUpdateNeeded();
        }
        return;
    }
    if (!m_cliReady) {
        emit cliReadyCheckNeeded();
        return;
    }
    if (m_layoutRunner && m_layoutRunner->isRunning()) {
        m_layoutRunPending = true;
        m_layoutRunPendingQuiet = quiet;
        return;
    }

    const QString requestedProfile = m_cfg.profileCombo ? m_cfg.profileCombo->currentData().toString() : QString();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = m_cfg.context
        ? m_cfg.context->selectedProfileDefinition(selectedProfile)
        : false;

    LayoutRunConfig config;
    config.sourceFolderPath = inputCfg.sourceFolderPath;
    config.imagePathList    = inputCfg.imagePathList;
    config.layoutBinary     = m_cfg.layoutBinary;

    if (hasSelectedProfile) {
        config.profile = selectedProfile;
    }

    config.scale = 1.0;

    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    if (m_cfg.resolutionCombo) {
        parseResolutionText(m_cfg.resolutionCombo->currentText(), sourceResolutionWidth, sourceResolutionHeight);
    }
    config.sourceResolutionWidth = sourceResolutionWidth;
    config.sourceResolutionHeight = sourceResolutionHeight;
    config.retryWithoutTrim = m_retryWithoutTrimOnFailure;
    config.deduplicateMode = m_deduplicateMode;

    m_cfg.session->lastRunUsedTrim = (hasSelectedProfile ? selectedProfile.trimTransparent : false) && !m_retryWithoutTrimOnFailure;
    m_runningLayoutProfile = requestedProfile;
    m_isCanceled = false;
    emit statusMessageChanged(tr("Rebuilding layout..."));
    m_layoutFailureDialogShown = false;

    if (!config.imagePathList.isEmpty()) {
        qInfo() << "[Layout] Dispatching run via --stdin-list,"
                << "paths=" << config.imagePathList.size();
    } else {
        qInfo() << "[Layout] Dispatching run, sourceFolderPath=" << config.sourceFolderPath
                << "exists=" << QFileInfo(config.sourceFolderPath).exists()
                << "isDir=" << QFileInfo(config.sourceFolderPath).isDir();
    }

    qInfo() << "[Performance] LayoutOrchestrator::run preparation took" << totalTimer.elapsed() << "ms";
    m_layoutRunner->run(config);
}

// --- isProfileEnabled ---

bool LayoutOrchestrator::isProfileEnabled(const QString& profile) const {
    if (!m_cfg.profileCombo) return false;
    const int index = m_cfg.profileCombo->findData(profile);
    if (index < 0) {
        return false;
    }
    if (const auto* model = qobject_cast<const QStandardItemModel*>(m_cfg.profileCombo->model())) {
        const QStandardItem* item = model->item(index);
        return item && item->isEnabled();
    }
    const QVariant enabled = m_cfg.profileCombo->itemData(index, Qt::UserRole - 1);
    return !enabled.isValid() || enabled.toBool();
}

// --- handleProfileFailure ---

void LayoutOrchestrator::handleProfileFailure(const QString& failedProfile) {
    if (!m_cfg.profileCombo) return;
    const int failedIndex = m_cfg.profileCombo->findData(failedProfile);
    if (failedIndex >= 0) {
        if (auto* model = qobject_cast<QStandardItemModel*>(m_cfg.profileCombo->model())) {
            if (QStandardItem* item = model->item(failedIndex)) {
                item->setEnabled(false);
            }
        } else {
            m_cfg.profileCombo->setItemData(failedIndex, 0, Qt::UserRole - 1);
        }
    }

    QString fallbackProfile;
    if (!m_cfg.session->lastSuccessfulProfile.isEmpty() &&
        m_cfg.session->lastSuccessfulProfile != failedProfile &&
        isProfileEnabled(m_cfg.session->lastSuccessfulProfile)) {
        fallbackProfile = m_cfg.session->lastSuccessfulProfile;
    } else if (failedProfile != "fast" && isProfileEnabled("fast")) {
        fallbackProfile = "fast";
    } else {
        for (int i = 0; i < m_cfg.profileCombo->count(); ++i) {
            QString candidate = m_cfg.profileCombo->itemData(i).toString();
            if (candidate == failedProfile) {
                continue;
            }
            if (isProfileEnabled(candidate)) {
                fallbackProfile = candidate;
                break;
            }
        }
    }

    if (fallbackProfile.isEmpty()) {
        MessageDialog::warning(nullptr, tr("Profile disabled"),
            tr("The selected profile failed and was disabled. No fallback profile is available."));
        return;
    }
    if (m_cfg.profileCombo->currentData().toString() == fallbackProfile) {
        run();
        return;
    }

    emit profileFallbackRequested(fallbackProfile);
}

// --- handleDimensionsError ---

void LayoutOrchestrator::handleDimensionsError(const QString& failedProfile) {
    if (!m_cfg.profileCombo) return;

    QVector<SpratProfile> allProfiles;
    if (m_cfg.context) {
        allProfiles = m_cfg.context->configuredProfiles();
    }

    bool failedHasMultipack = false;
    int failedArea = 0;
    for (const SpratProfile& p : allProfiles) {
        if (p.name.trimmed() == failedProfile) {
            failedHasMultipack = p.multipack;
            failedArea = p.maxWidth * p.maxHeight;
            break;
        }
    }

    auto profileDef = [&](const QString& name) -> const SpratProfile* {
        for (const SpratProfile& p : allProfiles)
            if (p.name.trimmed() == name) return &p;
        return nullptr;
    };

    QString fallback;

    if (!failedHasMultipack) {
        for (int i = 0; i < m_cfg.profileCombo->count() && fallback.isEmpty(); ++i) {
            const QString c = m_cfg.profileCombo->itemData(i).toString();
            if (c == failedProfile || !isProfileEnabled(c)) continue;
            if (const SpratProfile* d = profileDef(c); d && d->multipack)
                fallback = c;
        }
    }

    if (fallback.isEmpty()) {
        int bestArea = failedArea;
        for (int i = 0; i < m_cfg.profileCombo->count(); ++i) {
            const QString c = m_cfg.profileCombo->itemData(i).toString();
            if (c == failedProfile || !isProfileEnabled(c)) continue;
            if (const SpratProfile* d = profileDef(c)) {
                int area = d->maxWidth * d->maxHeight;
                if (area > bestArea) { bestArea = area; fallback = c; }
            }
        }
    }

    if (fallback.isEmpty()) {
        for (int i = 0; i < m_cfg.profileCombo->count() && fallback.isEmpty(); ++i) {
            const QString c = m_cfg.profileCombo->itemData(i).toString();
            if (c != failedProfile && isProfileEnabled(c)) fallback = c;
        }
    }

    if (fallback.isEmpty()) {
        emit statusMessageChanged(tr("Error running layout"));
        MessageDialog::critical(nullptr, tr("Error"),
            tr("Sprite dimensions exceed atlas limits and no suitable fallback profile is available."));
        m_layoutFailureDialogShown = true;
    }

    emit statusMessageChanged(
        tr("Profile '%1' failed (sprite too large), retrying with '%2'...")
            .arg(failedProfile, fallback));

    if (m_cfg.profileCombo->currentData().toString() == fallback)
        run();
    else
        emit profileFallbackRequested(fallback);
}

// --- onRunnerFinished ---

void LayoutOrchestrator::onRunnerFinished(const LayoutResult& result) {
    if (!result.success) {
        const QString failedProfile = m_runningLayoutProfile;

        if (result.wasKilledIntentionally) {
            m_runningLayoutProfile.clear();
            if (m_layoutRunPending) {
                const bool q = m_layoutRunPendingQuiet;
                m_layoutRunPending = false;
                m_layoutRunPendingQuiet = false;
                run(q);
            }
            return;
        }

        const QString combined = (result.error + "\n" + result.output).toLower();

        if (combined.contains("sprite dimensions exceed")) {
            handleDimensionsError(failedProfile);
            m_runningLayoutProfile.clear();
            return;
        }

        m_runningLayoutProfile.clear();
        if (!m_retryWithoutTrimOnFailure &&
            !m_layoutRunPending &&
            m_cfg.session->lastRunUsedTrim &&
            combined.contains("failed to compute compact layout")) {
            emit statusMessageChanged(tr("Retrying without trim transparency..."));
            m_retryWithoutTrimOnFailure = true;
            run();
            return;
        }

        if (!failedProfile.isEmpty() && !m_profilesTriedForCurrentLoad.contains(failedProfile))
            m_profilesTriedForCurrentLoad.append(failedProfile);

        QString nextProfile;
        if (m_cfg.profileCombo) {
            for (int i = 0; i < m_cfg.profileCombo->count(); ++i) {
                const QString candidate = m_cfg.profileCombo->itemData(i).toString();
                if (!m_profilesTriedForCurrentLoad.contains(candidate) && isProfileEnabled(candidate)) {
                    nextProfile = candidate;
                    break;
                }
            }
        }

        if (!nextProfile.isEmpty()) {
            emit statusMessageChanged(tr("Profile '%1' failed, trying '%2'...")
                .arg(failedProfile, nextProfile));
            emit profileFallbackRequested(nextProfile);
            m_retryWithoutTrimOnFailure = false;
            m_layoutRunPending = false;
            m_layoutRunPendingQuiet = false;
            run(true);
            return;
        }

        m_profilesTriedForCurrentLoad.clear();

        QString details = result.error;
        if (details.isEmpty()) details = result.output;
        if (details.isEmpty()) details = tr("spratlayout exited with code %1.").arg(result.exitCode);

        emit statusMessageChanged(tr("Error running layout"));
        emit loadingStateChanged(false);
        qCritical() << "spratlayout process failed. Exit code:" << result.exitCode
                    << "Error:" << result.error << "Output:" << result.output;

        if (!m_layoutFailureDialogShown && !result.error.contains("stopped", Qt::CaseInsensitive)) {
            if (!result.output.isEmpty() || !result.error.isEmpty()) {
                MessageDialog::critical(nullptr, tr("Error"), tr("spratlayout failed:\n") + details);
                m_layoutFailureDialogShown = true;
            }
        }

        m_retryWithoutTrimOnFailure = false;
        if (m_layoutRunPending) {
            const bool q = m_layoutRunPendingQuiet;
            m_layoutRunPending = false;
            m_layoutRunPendingQuiet = false;
            run(q);
        }
        return;
    }

    m_profilesTriedForCurrentLoad.clear();
    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;

    const QString layoutText = result.output;
    QElapsedTimer parseTimer;
    parseTimer.start();

    const QString parserFolder = m_cfg.context ? m_cfg.context->layoutParserFolder() : QString();
    QVector<LayoutModel> newModels = LayoutParser::parse(layoutText, parserFolder,
                                                         m_cfg.session->currentFolder);
    qInfo() << "[Layout] LayoutParser::parse done"
            << "models=" << newModels.size()
            << "ms=" << parseTimer.elapsed();
    if (newModels.isEmpty()) {
        newModels.append(LayoutModel());
    }

    m_cfg.session->cachedLayoutOutput = layoutText;
    m_cfg.session->cachedLayoutScale = newModels.first().scale;

    const bool forceCenter = m_centerPivotsOnNextLayout;
    m_centerPivotsOnNextLayout = false;

    auto legacyDefaultPivotX = [](const SpritePtr& sprite) -> int {
        return sprite ? (sprite->rect.width() / 2) : 0;
    };
    auto legacyDefaultPivotY = [](const SpritePtr& sprite) -> int {
        return sprite ? (sprite->rect.height() / 2) : 0;
    };

    if (m_cfg.session->pendingProjectPayload.isEmpty()) {
        QMap<QString, SpritePtr> oldSprites;
        for (const auto& model : m_cfg.session->activeAtlas().layoutModels) {
            for (const auto& s : model.sprites) {
                oldSprites[s->path] = s;
            }
        }
        for (auto& model : newModels) {
            for (auto& s : model.sprites) {
                if (!oldSprites.contains(s->path)) {
                    continue;
                }
                auto oldS = oldSprites[s->path];
                s->name    = oldS->name;
                s->aliases = oldS->aliases;
                if (!forceCenter) {
                    const bool oldPivotIsLegacyDefault =
                        oldS->pivotX == legacyDefaultPivotX(oldS) &&
                        oldS->pivotY == legacyDefaultPivotY(oldS);
                    if (!oldPivotIsLegacyDefault) {
                        s->pivotX = oldS->pivotX;
                        s->pivotY = oldS->pivotY;
                    }
                }
                s->points = oldS->points;
            }
        }
    }

    for (auto& model : newModels) {
        for (auto& s : model.sprites) {
            if (s->pivotX == 0 && s->pivotY == 0 && s->rect.width() > 0) {
                const int contentW = s->rotated ? s->rect.height() : s->rect.width();
                const int contentH = s->rotated ? s->rect.width()  : s->rect.height();
                if (s->trimmed) {
                    s->pivotX = (s->trimRect.x() + contentW + s->trimRect.width())  / 2;
                    s->pivotY = (s->trimRect.y() + contentH + s->trimRect.height()) / 2;
                } else {
                    s->pivotX = contentW / 2;
                    s->pivotY = contentH / 2;
                }
            }
        }
    }

    ensureUniqueSpriteNames(newModels, m_cfg.session->sourceFolder);

    m_cfg.session->activeAtlas().layoutModels = newModels;
    m_cfg.session->rebuildSpriteIndex();
    AnimationPreviewService::invalidateSpriteMap();
    emit activeFrameListUpdateNeeded();
    emit initSourceFolderWatcherNeeded();

    const int myGeneration = ++m_layoutGeneration;

    QStringList selectedPaths;
    for (const auto& s : m_cfg.session->selectedSprites) {
        selectedPaths << s->path;
    }
    const QString primaryPath = m_cfg.session->selectedSprite
        ? m_cfg.session->selectedSprite->path : QString();

    auto newScenePositions = LayoutCanvas::computeItemScenePositions(newModels);
    auto newAtlasRects     = LayoutCanvas::computeAtlasRects(newModels);

    auto doSetModels = [this, newModels, selectedPaths, primaryPath, myGeneration]() {
        if (myGeneration != m_layoutGeneration) {
            qInfo() << "[Layout] Skipping stale canvas update for generation" << myGeneration
                    << "(current:" << m_layoutGeneration << ")";
            return;
        }
        qInfo() << "[Layout] setModelsAsync start"
                << "models=" << m_cfg.session->activeAtlas().layoutModels.size();
        m_cfg.canvas->setModelsAsync(newModels, &m_isCanceled,
            [this, selectedPaths, primaryPath, myGeneration]() {
                if (m_isCanceled) {
                    emit statusMessageChanged(tr("Loading images canceled"));
                    return;
                }

                if (myGeneration != m_layoutGeneration) {
                    qInfo() << "[Layout] Skipping stale UI update for generation" << myGeneration;
                    m_pendingChangeCount = 0;
                    if (m_layoutRunPending) {
                        const bool q = m_layoutRunPendingQuiet;
                        m_layoutRunPending = false;
                        m_layoutRunPendingQuiet = false;
                        run(q);
                    }
                    return;
                }

                const QString currentProfile = m_cfg.profileCombo
                    ? m_cfg.profileCombo->currentData().toString() : QString();
                const bool profileChanged = m_cfg.session->lastSuccessfulProfile != currentProfile;

                switch (m_layoutZoomOnChange) {
                case LayoutZoomOnChange::Fit:
                    m_cfg.canvas->setZoomManual(false);
                    QTimer::singleShot(0, m_cfg.canvas, &LayoutCanvas::initialFit);
                    break;
                case LayoutZoomOnChange::Reset100:
                    m_cfg.canvas->setZoomManual(false);
                    QTimer::singleShot(0, m_cfg.canvas, [this]() { m_cfg.canvas->setZoom(1.0); });
                    break;
                case LayoutZoomOnChange::NoChange:
                default:
                    if (profileChanged) {
                        m_cfg.canvas->setZoomManual(false);
                        QTimer::singleShot(0, m_cfg.canvas, &LayoutCanvas::initialFit);
                    }
                    break;
                }

                emit statusMessageChanged(QString(tr("Loaded %1 sprites in %2 atlas(es)"))
                    .arg(m_cfg.session->activeFramePaths.size())
                    .arg(m_cfg.session->activeAtlas().layoutModels.size()));

                QStringList parts;
                for (const auto& model : m_cfg.session->activeAtlas().layoutModels)
                    parts << QString("%1 \xc3\x97 %2").arg(model.atlasWidth).arg(model.atlasHeight);
                emit atlasDimsUpdated(parts.join(QStringLiteral(", ")) + QStringLiteral(" px"));

                emit loadingStateChanged(false);

                if (m_cfg.session->layoutSourceIsList) {
                    emit manualFrameLabelUpdateNeeded();
                }

                if (!m_cfg.session->pendingProjectPayload.isEmpty()) {
                    emit pendingProjectPayloadReady();
                } else if (!selectedPaths.isEmpty()) {
                    emit selectSpritesByPathsRequested(selectedPaths, primaryPath);
                }

                if (m_cfg.profileCombo)
                    m_cfg.session->lastSuccessfulProfile = m_cfg.profileCombo->currentData().toString();

                emit spriteTreeRefreshNeeded();
                emit mainContentViewUpdateNeeded();
                emit uiUpdateNeeded();
                emit openSourceFolderActionUpdateNeeded();
                emit cliDiagnosticsUpdateNeeded();

                emit tempDirsCleanupNeeded();

                m_pendingChangeCount = 0;
                qInfo() << "[Layout] setModelsAsync finished";

                if (m_layoutRunPending) {
                    const bool q = m_layoutRunPendingQuiet;
                    m_layoutRunPending = false;
                    m_layoutRunPendingQuiet = false;
                    run(q);
                }
            });
    };

    animateToNewPositions(newScenePositions, newAtlasRects, newModels, doSetModels);
}

// --- onRunnerError ---

void LayoutOrchestrator::onRunnerError(const QString& details) {
    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;

    emit statusMessageChanged(tr("Error running layout process"));
    emit loadingStateChanged(false);
    qCritical() << "spratlayout process error:" << details;

    if (!m_layoutFailureDialogShown) {
        MessageDialog::critical(nullptr, tr("Error"),
            tr("spratlayout process failed:\n") + details);
        m_layoutFailureDialogShown = true;
    }

    if (m_layoutRunPending) {
        const bool q = m_layoutRunPendingQuiet;
        m_layoutRunPending = false;
        m_layoutRunPendingQuiet = false;
        run(q);
    }
}

// --- animateToNewPositions ---

void LayoutOrchestrator::animateToNewPositions(
    const QMap<QString, QPointF>& newPositions,
    const QVector<QRectF>& newAtlasRects,
    const QVector<LayoutModel>& newModels,
    std::function<void()> onFinished)
{
    if (!m_enableSpriteAnimation || m_oldSpritePositions.isEmpty() || !m_cfg.canvas) {
        m_oldSpritePositions.clear();
        m_oldSpritePackedRects.clear();
        m_oldSpriteRotated.clear();
        onFinished();
        return;
    }

    if (m_spriteAnimation) {
        m_spriteAnimation->stop();
        for (const auto& path : m_spriteAnimationPaths)
            m_cfg.canvas->setSpriteItemLabelHidden(path, false);
        m_spriteAnimationPaths.clear();
        delete m_spriteAnimation.data();
        capturePositions();
    }

    QMap<QString, SpritePtr> newSpriteLookup;
    for (const auto& model : newModels)
        for (const auto& sprite : model.sprites)
            if (sprite) newSpriteLookup[sprite->path] = sprite;

    struct Move {
        QString path;
        QPointF oldPos;
        QPointF newPos;
        qreal   endAngle;
        QPointF transformOrigin;
    };
    QVector<Move> moves;

    for (auto it = newPositions.begin(); it != newPositions.end(); ++it) {
        const QString& path = it.key();
        if (!m_oldSpritePositions.contains(path)) continue;

        const QPointF oldPos    = m_oldSpritePositions[path];
        const QPointF rawNewPos = it.value();
        const bool    oldRotated = m_oldSpriteRotated.value(path, false);
        const QRect   oldRect    = m_oldSpritePackedRects.value(path);
        const int     iw = oldRect.width();
        const int     ih = oldRect.height();

        const SpritePtr newSprite = newSpriteLookup.value(path);
        const bool newRotated = newSprite ? newSprite->rotated : oldRotated;

        qreal   endAngle = 0.0;
        QPointF adjustedNewPos = rawNewPos;
        QPointF origin;

        if (oldRotated != newRotated && iw > 0 && ih > 0) {
            endAngle = oldRotated ? -90.0 : 90.0;
            origin   = QPointF(iw / 2.0, ih / 2.0);
            adjustedNewPos = rawNewPos + QPointF((ih - iw) / 2.0, (iw - ih) / 2.0);
        }

        const QPointF delta = adjustedNewPos - oldPos;
        if (qAbs(delta.x()) < 0.5 && qAbs(delta.y()) < 0.5 && qAbs(endAngle) < 0.01)
            continue;

        moves.append({path, oldPos, adjustedNewPos, endAngle, origin});
    }

    m_oldSpritePositions.clear();
    m_oldSpritePackedRects.clear();
    m_oldSpriteRotated.clear();

    struct AtlasMove { int index; QRectF oldRect; QRectF newRect; };
    QVector<AtlasMove> atlasMoves;
    const QVector<QRectF> oldAtlasRects = m_cfg.canvas->currentAtlasRects();
    const int atlasCount = qMin(oldAtlasRects.size(), newAtlasRects.size());
    for (int i = 0; i < atlasCount; ++i) {
        const QRectF& o = oldAtlasRects[i];
        const QRectF& n = newAtlasRects[i];
        if (qAbs(o.x()-n.x())>0.5 || qAbs(o.y()-n.y())>0.5 ||
            qAbs(o.width()-n.width())>0.5 || qAbs(o.height()-n.height())>0.5)
            atlasMoves.append({i, o, n});
    }

    const QRectF oldSceneRect = m_cfg.canvas->scene()->sceneRect();
    QRectF newSceneRect = oldSceneRect;
    if (!newAtlasRects.isEmpty()) {
        qreal maxW = 0;
        for (const auto& r : newAtlasRects)
            maxW = qMax(maxW, r.width());
        const QRectF& last = newAtlasRects.last();
        newSceneRect = QRectF(0, 0, maxW, last.y() + last.height());
    }
    const bool sceneRectChanges =
        qAbs(oldSceneRect.width()  - newSceneRect.width())  > 0.5 ||
        qAbs(oldSceneRect.height() - newSceneRect.height()) > 0.5;

    if (moves.isEmpty() && atlasMoves.isEmpty() && !sceneRectChanges) {
        onFinished();
        return;
    }

    for (const auto& move : moves)
        if (qAbs(move.endAngle) > 0.01)
            m_cfg.canvas->setSpriteItemTransformOrigin(move.path, move.transformOrigin);

    m_spriteAnimationPaths.clear();
    for (const auto& move : moves) {
        m_cfg.canvas->setSpriteItemLabelHidden(move.path, true);
        m_spriteAnimationPaths.append(move.path);
    }

    m_spriteAnimation = new QVariantAnimation(this);
    auto* anim = m_spriteAnimation.data();
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);

    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, moves, atlasMoves, oldSceneRect, newSceneRect](const QVariant& v) {
        const double t = v.toDouble();
        for (const auto& move : moves) {
            m_cfg.canvas->setSpriteItemScenePos(move.path,
                move.oldPos + t * (move.newPos - move.oldPos));
            if (qAbs(move.endAngle) > 0.01)
                m_cfg.canvas->setSpriteItemRotation(move.path, t * move.endAngle);
        }
        for (const auto& am : atlasMoves)
            m_cfg.canvas->setAtlasRect(am.index, QRectF(
                am.oldRect.x()      + t * (am.newRect.x()      - am.oldRect.x()),
                am.oldRect.y()      + t * (am.newRect.y()      - am.oldRect.y()),
                am.oldRect.width()  + t * (am.newRect.width()  - am.oldRect.width()),
                am.oldRect.height() + t * (am.newRect.height() - am.oldRect.height())));
        m_cfg.canvas->scene()->setSceneRect(QRectF(
            0, 0,
            oldSceneRect.width()  + t * (newSceneRect.width()  - oldSceneRect.width()),
            oldSceneRect.height() + t * (newSceneRect.height() - oldSceneRect.height())));
        m_cfg.canvas->viewport()->update();
    });

    connect(anim, &QVariantAnimation::finished, this,
            [this, moves, atlasMoves, newSceneRect, onFinished]() {
        for (const auto& move : moves) {
            m_cfg.canvas->setSpriteItemScenePos(move.path, move.newPos);
            if (qAbs(move.endAngle) > 0.01)
                m_cfg.canvas->setSpriteItemRotation(move.path, move.endAngle);
            m_cfg.canvas->setSpriteItemLabelHidden(move.path, false);
        }
        for (const auto& am : atlasMoves)
            m_cfg.canvas->setAtlasRect(am.index, am.newRect);
        m_cfg.canvas->scene()->setSceneRect(newSceneRect);
        m_cfg.canvas->viewport()->update();
        m_spriteAnimationPaths.clear();
        onFinished();
    });

    connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
}
