#include "MainWindow.h"
#include "UndoCommands.h"

#include "SpriteSelectionPresenter.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include "LayoutRunner.h"
#include "LayoutParser.h"
#include "ResolutionUtils.h"
#include "SpriteNameUtils.h"
#include "AppConstants.h"
#include "AnimationPreviewService.h"
#include "MessageDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QStyle>
#include <QCoreApplication>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QDockWidget>
#include <QVariantAnimation>
#include <QEasingCurve>

namespace {
int legacyDefaultPivotX(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.width() / 2) : 0;
}

int legacyDefaultPivotY(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.height() / 2) : 0;
}
}

LayoutRunConfig MainWindow::buildLayoutInput() const {
    LayoutRunConfig cfg;

    // New model: use sources if populated.
    if (!m_session->sources.isEmpty()) {
        const bool singleFolderOptimised =
            m_session->sources.size() == 1
            && m_session->sources.first().type == SourceType::Folder
            && m_settings.syncMode != SyncMode::None
            && !m_session->sourceFolder.isEmpty()
            && sourceFolderMatchesActiveFrames();
        if (singleFolderOptimised) {
            // Use the working temp copy rather than originalPath; keeps activeFramePaths
            // consistent with sourceFolder and avoids "Invalid image path" on subsequent runs.
            cfg.sourceFolderPath = m_session->sourceFolder;
        } else if (!m_session->activeFramePaths.isEmpty()) {
            cfg.imagePathList = m_session->activeFramePaths;
        } else if (!m_session->layoutSourcePath.isEmpty()) {
            // Fallback for initial load: activeFramePaths not yet populated; use the
            // copied source folder that loadFolder() / processExtractedFrames() set.
            cfg.sourceFolderPath = m_session->layoutSourcePath;
        }
        return cfg;
    }

    // Backward-compat path: old sourceFolder / layoutSourcePath / layoutSourceIsList approach.
    if (m_settings.syncMode != SyncMode::None && sourceFolderMatchesActiveFrames()) {
        cfg.sourceFolderPath = m_session->sourceFolder;
    } else if (m_session->layoutSourceIsList && !m_session->activeFramePaths.isEmpty()) {
        // Use stdin-list instead of the temp file — avoids temp file I/O.
        cfg.imagePathList = m_session->activeFramePaths;
    } else if (!m_session->layoutSourcePath.isEmpty()) {
        cfg.sourceFolderPath = m_session->layoutSourcePath;
    }
    return cfg;
}

void MainWindow::onRunLayout(bool quiet) {
    QElapsedTimer totalTimer;
    totalTimer.start();

    // If sync is active, ensure active frames have been copied to the sprites folder.
    if (m_settings.syncMode != SyncMode::None
        && m_session
        && !m_session->sourceFolder.isEmpty()
        && !m_session->activeFramePaths.isEmpty()
        && !activeFramesAreInSourceFolder()) {

        QElapsedTimer syncTimer;
        syncTimer.start();
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        qInfo() << "[Performance] onRunLayout sync/copy took" << syncTimer.elapsed() << "ms";
    }

    const LayoutRunConfig inputCfg = buildLayoutInput();
    if (inputCfg.sourceFolderPath.isEmpty() && inputCfg.imagePathList.isEmpty()) {
        return;
    }
    if (!m_cliReady) {
        checkCliTools();
        return;
    }
    if (m_layoutRunner && m_layoutRunner->isRunning()) {
        m_layoutRunPending = true;
        m_layoutRunPendingQuiet = quiet;
        return;
    }

    const QString requestedProfile = m_profileCombo->currentData().toString();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);

    LayoutRunConfig config;
    config.sourceFolderPath = inputCfg.sourceFolderPath;
    config.imagePathList    = inputCfg.imagePathList;
    config.layoutBinary = m_spratLayoutBin;

    if (hasSelectedProfile) {
        config.profile = selectedProfile;
    }

    config.scale = 1.0;

    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    if (m_sourceResolutionCombo) {
        parseResolutionText(m_sourceResolutionCombo->currentText(), sourceResolutionWidth, sourceResolutionHeight);
    }
    config.sourceResolutionWidth = sourceResolutionWidth;
    config.sourceResolutionHeight = sourceResolutionHeight;
    config.retryWithoutTrim = m_retryWithoutTrimOnFailure;
    config.deduplicateMode = m_settings.deduplicateMode;

    m_session->lastRunUsedTrim = (hasSelectedProfile ? selectedProfile.trimTransparent : false) && !m_retryWithoutTrimOnFailure;
    m_runningLayoutProfile = requestedProfile;
    m_isCanceled = false;
    // Layout rebuilds silently in the background - no loading dialog or cursor
    m_statusLabel->setText(tr("Rebuilding layout..."));
    m_layoutFailureDialogShown = false;

    if (!config.imagePathList.isEmpty()) {
        qInfo() << "[Layout] Dispatching run via --stdin-list,"
                << "paths=" << config.imagePathList.size();
    } else {
        qInfo() << "[Layout] Dispatching run, sourceFolderPath=" << config.sourceFolderPath
                << "exists=" << QFileInfo(config.sourceFolderPath).exists()
                << "isDir=" << QFileInfo(config.sourceFolderPath).isDir();
    }
    
    qInfo() << "[Performance] onRunLayout preparation took" << totalTimer.elapsed() << "ms";
    m_layoutRunner->run(config);
}

void MainWindow::onLayoutFinished(const LayoutResult& result) {
    if (!result.success) {
        const QString failedProfile = m_runningLayoutProfile;

        if (result.wasKilledIntentionally) {
            m_runningLayoutProfile.clear();
            if (m_layoutRunPending) {
                const bool q = m_layoutRunPendingQuiet;
                m_layoutRunPending = false;
                m_layoutRunPendingQuiet = false;
                onRunLayout(q);
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
            m_session->lastRunUsedTrim &&
            combined.contains("failed to compute compact layout")) {
            m_statusLabel->setText(tr("Retrying without trim transparency..."));
            m_retryWithoutTrimOnFailure = true;
            onRunLayout();
            return;
        }

        // Profile fallback: try every other enabled profile before showing an error dialog.
        if (!failedProfile.isEmpty() && !m_profilesTriedForCurrentLoad.contains(failedProfile))
            m_profilesTriedForCurrentLoad.append(failedProfile);

        QString nextProfile;
        for (int i = 0; i < m_profileCombo->count(); ++i) {
            const QString candidate = m_profileCombo->itemData(i).toString();
            if (!m_profilesTriedForCurrentLoad.contains(candidate) && isProfileEnabled(candidate)) {
                nextProfile = candidate;
                break;
            }
        }

        if (!nextProfile.isEmpty()) {
            m_statusLabel->setText(tr("Profile '%1' failed, trying '%2'...")
                .arg(failedProfile, nextProfile));
            {
                const QSignalBlocker blocker(m_profileCombo);
                m_profileCombo->setCurrentIndex(m_profileCombo->findData(nextProfile));
                m_currentProfile = nextProfile;
            }
            m_retryWithoutTrimOnFailure = false;
            m_layoutRunPending = false;
            m_layoutRunPendingQuiet = false;
            onRunLayout(true);
            return;
        }

        // All profiles exhausted — reset tracking and show error dialog.
        m_profilesTriedForCurrentLoad.clear();

        QString details = result.error;
        if (details.isEmpty()) details = result.output;
        if (details.isEmpty()) details = tr("spratlayout exited with code %1.").arg(result.exitCode);

        m_statusLabel->setText(tr("Error running layout"));
        setLoading(false);
        qCritical() << "spratlayout process failed. Exit code:" << result.exitCode << "Error:" << result.error << "Output:" << result.output;

        // Don't show error dialog if it was retrying or if it was explicitly stopped (which often results in non-zero exit code but we don't want a dialog)
        if (!m_layoutFailureDialogShown && !result.error.contains("stopped", Qt::CaseInsensitive)) {
            // Check if it was killed by us (no output and no error usually means killed)
            if (!result.output.isEmpty() || !result.error.isEmpty()) {
                MessageDialog::critical(this, tr("Error"), tr("spratlayout failed:\n") + details);
                m_layoutFailureDialogShown = true;
            }
        }

        m_retryWithoutTrimOnFailure = false;
        if (m_layoutRunPending) {
            const bool q = m_layoutRunPendingQuiet;
            m_layoutRunPending = false;
            m_layoutRunPendingQuiet = false;
            onRunLayout(q);
        }
        return;
    }

    m_profilesTriedForCurrentLoad.clear();
    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;

    const QString layoutText = result.output;
    QElapsedTimer parseTimer;
    parseTimer.start();
    QVector<LayoutModel> newModels = LayoutParser::parse(layoutText, layoutParserFolder(),
                                                         m_session->currentFolder);
    qInfo() << "[WASM] LayoutParser::parse done"
            << "models=" << newModels.size()
            << "ms=" << parseTimer.elapsed();
    if (newModels.isEmpty()) {
        newModels.append(LayoutModel());
    }

    m_session->cachedLayoutOutput = layoutText;
    m_session->cachedLayoutScale = newModels.first().scale;

    // Consume the flag before the merge loop so it can gate pivot preservation below.
    const bool forceCenter = m_centerPivotsOnNextLayout;
    m_centerPivotsOnNextLayout = false;

    if (m_session->pendingProjectPayload.isEmpty()) {
        QMap<QString, SpritePtr> oldSprites;
        for (const auto& model : m_session->layoutModels) {
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
                // When force-centering (fresh content load), skip pivot preservation so that
                // LayoutParser's rotation-aware, QImageReader-based pivot is kept intact.
                // On incremental re-layouts, preserve user-edited pivots as before.
                if (!forceCenter) {
                    const bool oldPivotIsLegacyDefault =
                        oldS->pivotX == legacyDefaultPivotX(oldS) &&
                        oldS->pivotY == legacyDefaultPivotY(oldS);
                    // Preserve user-edited pivots; let legacy auto pivots upgrade to the new default.
                    if (!oldPivotIsLegacyDefault) {
                        s->pivotX = oldS->pivotX;
                        s->pivotY = oldS->pivotY;
                    }
                }
                s->points = oldS->points;
            }
        }
    }

    // Safety-net centering pass: if LayoutParser somehow left a pivot at (0, 0) for a
    // non-empty sprite (shouldn't happen on non-WASM, can occur on WASM for non-trimmed
    // sprites whose image read fails), nudge it to the source-image centre.
    // For rotated sprites the atlas rect has width and height swapped relative to the
    // source image, so content dimensions must be derived in source-image space.
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

    ensureUniqueSpriteNames(newModels, m_session->sourceFolder);

    // Update session state immediately — before the animation starts.
    // This prevents a race condition where the watcher's debounce fires during
    // the ~180 ms animation window and performManualSync() reads stale models
    // (which would include the just-deleted sprite and trigger a second layout run).
    m_session->layoutModels = newModels;
    AnimationPreviewService::invalidateSpriteMap();
    populateActiveFrameListFromModel();
    // Ensure the file watcher is tracking the current source folder.
    // Covers all load paths (folder, ZIP, image, project restore).
    initializeSourceFolderWatcher();

    // Bump the generation counter so that any doSetModels lambda still in flight
    // from a previous run can detect it is stale and skip the canvas update.
    const int myGeneration = ++m_layoutGeneration;

    QStringList selectedPaths;
    for (const auto& s : m_session->selectedSprites) {
        selectedPaths << s->path;
    }
    const QString primaryPath = m_session->selectedSprite ? m_session->selectedSprite->path : QString();

    auto newScenePositions = LayoutCanvas::computeItemScenePositions(newModels);
    auto newAtlasRects     = LayoutCanvas::computeAtlasRects(newModels);

    // Capture the post-animation canvas update as a lambda.
    // Session state (layoutModels, activeFramePaths, watcher) was already updated above.
    auto doSetModels = [this, newModels, selectedPaths, primaryPath, myGeneration]() {
        // A newer layout run has already completed — skip this stale canvas update.
        // The newer run's doSetModels already owns the canvas.
        if (myGeneration != m_layoutGeneration) {
            qInfo() << "[Layout] Skipping stale canvas update for generation" << myGeneration
                    << "(current:" << m_layoutGeneration << ")";
            return;
        }
        if (m_isLoading) {
            m_loadingUiMessage = tr("Loading sprites...");
            if (m_cliInstallOverlayLabel) m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
        }
        qInfo() << "[WASM] setModelsAsync start"
                << "models=" << m_session->layoutModels.size();
        m_canvas->setModelsAsync(newModels, &m_isCanceled,
            [this, selectedPaths, primaryPath, myGeneration]() {
                if (m_isCanceled) {
                    m_statusLabel->setText(tr("Loading images canceled"));
                    return;
                }

                // Re-check: if a newer run completed while pixmaps were loading,
                // skip the UI updates that belong to this run.
                if (myGeneration != m_layoutGeneration) {
                    qInfo() << "[Layout] Skipping stale UI update for generation" << myGeneration;
                    m_pendingChangeCount = 0;
                    if (m_layoutRunPending) {
                        const bool q = m_layoutRunPendingQuiet;
                        m_layoutRunPending = false;
                        m_layoutRunPendingQuiet = false;
                        onRunLayout(q);
                    }
                    return;
                }

                const QString currentProfile = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
                const bool profileChanged = m_session->lastSuccessfulProfile != currentProfile;

                if (profileChanged) {
                    m_canvas->setZoomManual(false);
                    QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);
                }

                m_statusLabel->setText(QString(tr("Loaded %1 sprites in %2 atlas(es)"))
                    .arg(m_session->activeFramePaths.size())
                    .arg(m_session->layoutModels.size()));

                setLoading(false);

                // populateActiveFrameListFromModel() and initializeSourceFolderWatcher()
                // were already called immediately above (before the animation started).
                if (m_session->layoutSourceIsList) {
                    updateManualFrameLabel();
                }

                if (!m_session->pendingProjectPayload.isEmpty()) {
                    applyProjectPayload();
                } else if (!selectedPaths.isEmpty()) {
                    m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
                }

                m_session->lastSuccessfulProfile = m_profileCombo->currentData().toString();

                refreshSpriteTree();

                updateMainContentView();
                updateUiState();
                updateOpenSourceFolderAction();
                updateCliDiagnostics();

                m_session->clearTempDirs();

                m_pendingChangeCount = 0;
                qInfo() << "[WASM] setModelsAsync finished";

                if (m_layoutRunPending) {
                    const bool q = m_layoutRunPendingQuiet;
                    m_layoutRunPending = false;
                    m_layoutRunPendingQuiet = false;
                    onRunLayout(q);
                }
            });
    };

    animateSpritesToNewPositions(newScenePositions, newAtlasRects, newModels, doSetModels);
}

void MainWindow::onLayoutError(const QString& details) {
    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;

    m_statusLabel->setText(tr("Error running layout process"));
    setLoading(false);
    qCritical() << "spratlayout process error:" << details;
    
    if (!m_layoutFailureDialogShown) {
        MessageDialog::critical(this, tr("Error"), tr("spratlayout process failed:\n") + details);
        m_layoutFailureDialogShown = true;
    }
    
    if (m_layoutRunPending) {
        const bool q = m_layoutRunPendingQuiet;
        m_layoutRunPending = false;
        m_layoutRunPendingQuiet = false;
        onRunLayout(q);
    }
}

bool MainWindow::isProfileEnabled(const QString& profile) const {
    const int index = m_profileCombo->findData(profile);
    if (index < 0) {
        return false;
    }
    if (const auto* model = qobject_cast<const QStandardItemModel*>(m_profileCombo->model())) {
        const QStandardItem* item = model->item(index);
        return item && item->isEnabled();
    }
    const QVariant enabled = m_profileCombo->itemData(index, Qt::UserRole - 1);
    return !enabled.isValid() || enabled.toBool();
}

void MainWindow::handleProfileFailure(const QString& failedProfile) {
    const int failedIndex = m_profileCombo->findData(failedProfile);
    if (failedIndex >= 0) {
        if (auto* model = qobject_cast<QStandardItemModel*>(m_profileCombo->model())) {
            if (QStandardItem* item = model->item(failedIndex)) {
                item->setEnabled(false);
            }
        } else {
            m_profileCombo->setItemData(failedIndex, 0, Qt::UserRole - 1);
        }
    }

    QString fallbackProfile;
    if (!m_session->lastSuccessfulProfile.isEmpty() &&
        m_session->lastSuccessfulProfile != failedProfile &&
        isProfileEnabled(m_session->lastSuccessfulProfile)) {
        fallbackProfile = m_session->lastSuccessfulProfile;
    } else if (failedProfile != "fast" && isProfileEnabled("fast")) {
        fallbackProfile = "fast";
    } else {
        for (int i = 0; i < m_profileCombo->count(); ++i) {
            QString candidate = m_profileCombo->itemData(i).toString();
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
        MessageDialog::warning(this, tr("Profile disabled"), tr("The selected profile failed and was disabled. No fallback profile is available."));
        return;
    }
    if (m_profileCombo->currentData().toString() == fallbackProfile) {
        onRunLayout();
        return;
    }

    m_profileCombo->setCurrentIndex(m_profileCombo->findData(fallbackProfile));
}

void MainWindow::setLoading(bool loading) {
    m_isLoading = loading;
    auto showLoadingOverlayNow = [this]() {
        if (!m_cliInstallOverlay || !m_cliInstallOverlayLabel) {
            return;
        }
        m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
        if (m_cliInstallProgress) {
            m_cliInstallProgress->hide();
        }
        if (m_cliInstallLog) {
            m_cliInstallLog->hide();
        }
        if (m_cancelLoadingButton) {
            m_cancelLoadingButton->setVisible(!m_cliInstallInProgress);
        }
        m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        m_cliInstallOverlay->show();
        m_cliInstallOverlay->raise();
        m_loadingOverlayVisible = true;
    };
    if (loading) {
        if (m_welcomeLabel && (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty())) {
            m_welcomeLabel->setText(m_loadingUiMessage);
        }
        if (m_mainStack && m_welcomePage && (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty())) {
            m_mainStack->setCurrentWidget(m_welcomePage);
        }
        if (!m_cliInstallInProgress) {
            showLoadingOverlayNow();
        }
        // Disable dockers and canvas while loading - prevent interaction with stale data
        if (m_canvas) {
            m_canvas->setEnabled(false);
            // Create semi-transparent overlay if not already created
            if (!m_canvasOverlay) {
                m_canvasOverlay = new QWidget(m_canvas);
                m_canvasOverlay->setStyleSheet("background-color: rgba(128, 128, 128, 160);");
            }
            // Show overlay and resize to match canvas
            m_canvasOverlay->resize(m_canvas->size());
            m_canvasOverlay->raise();
            m_canvasOverlay->show();
        }
        if (m_atlasDock) m_atlasDock->setEnabled(false);
        if (m_animationDock) m_animationDock->setEnabled(false);
        if (m_debugDock) m_debugDock->setEnabled(false);
    } else {
        if (m_welcomeLabel) {
            m_welcomeLabel->setText(tr("Drag and drop a folder, image file, archive (zip/tar), or URL"));
        }
        if (!m_cliInstallInProgress && m_cliInstallOverlay) {
            m_cliInstallOverlay->hide();
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            m_loadingOverlayVisible = false;
        }
        // Re-enable dockers and canvas when loading finishes
        if (m_canvas) {
            m_canvas->setEnabled(true);
            // Hide overlay to restore normal canvas appearance
            if (m_canvasOverlay) {
                m_canvasOverlay->hide();
            }
        }
        if (m_atlasDock) m_atlasDock->setEnabled(true);
        if (m_animationDock) m_animationDock->setEnabled(true);
        if (m_debugDock) m_debugDock->setEnabled(true);
        m_loadingUiMessage = tr("Loading...");
    }
    if (m_statusProgressBar) {
        m_statusProgressBar->setVisible(loading);
    }
    setCursor(loading ? Qt::WaitCursor : Qt::ArrowCursor);
    updateUiState();
}

void MainWindow::onSpriteSelected(SpritePtr sprite) {
    clearCoordinateFieldOverride();
    m_session->selectedSprite = sprite;
    if (sprite) {
        m_statusLabel->setText(tr("Selected: ") + sprite->name);
    }
    SpriteSelectionPresenter::applySpriteSelection(
        sprite,
        m_session->selectedPointName,
        m_spriteNameEdit,
        m_pivotXSpin,
        m_pivotYSpin,
        m_configPointsBtn,
        m_previewView,
        m_previewZoomSpin,
        m_handleCombo,
        m_isRestoringProject);
    syncCoordinateSpinsFromSelection();

    if (m_editAliasesBtn) m_editAliasesBtn->setEnabled(sprite != nullptr);
    updateAliasesButton();
    updateOnionSkinDisplay();
}

void MainWindow::updateAliasesButton() {
    if (!m_editAliasesBtn) return;
    if (!m_session || !m_session->selectedSprite) {
        m_editAliasesBtn->setToolTip(tr("Edit sprite name aliases"));
        return;
    }
    const int count = m_session->selectedSprite->aliases.size();
    if (count > 0)
        m_editAliasesBtn->setToolTip(tr("Edit sprite name aliases (%1)").arg(count));
    else
        m_editAliasesBtn->setToolTip(tr("Edit sprite name aliases"));
}

void MainWindow::onSpriteNameEditingFinished() {
    if (!m_session || !m_session->selectedSprite || !m_spriteNameEdit) return;
    const QString newName = m_spriteNameEdit->text().trimmed();
    const QString oldName = m_session->selectedSprite->name;

    if (newName.isEmpty()) {
        // Revert display — don't allow clearing the name.
        m_spriteNameEdit->blockSignals(true);
        m_spriteNameEdit->setText(oldName);
        m_spriteNameEdit->blockSignals(false);
        return;
    }
    if (newName == oldName) return;

    const QStringList aliases = m_session->selectedSprite->aliases;
    m_session->selectedSprite->name = newName;
    if (m_statusLabel) m_statusLabel->setText(tr("Selected: ") + newName);

    SpritePtr sprite = m_session->selectedSprite;
    m_undoStack->push(new SetSpriteNamesCommand(
        sprite,
        oldName, aliases,
        newName, aliases,
        [this, sprite]() {
            if (m_session && m_session->selectedSprite == sprite) {
                m_spriteNameEdit->blockSignals(true);
                m_spriteNameEdit->setText(sprite->name);
                m_spriteNameEdit->blockSignals(false);
                if (m_statusLabel)
                    m_statusLabel->setText(tr("Selected: ") + sprite->name);
            }
        }
    ));
}

void MainWindow::onEditAliases() {
    if (!m_session || !m_session->selectedSprite) return;

    SpritePtr sprite            = m_session->selectedSprite;
    const QString canonicalName = sprite->name;
    const QStringList oldAliases = sprite->aliases;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Aliases for \"%1\"").arg(canonicalName));
    auto* layout = new QVBoxLayout(&dlg);

    auto* descLabel = new QLabel(tr("Aliases are alternative names for this sprite. "
                                    "They point to the same image and share all markers and pivots."), &dlg);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: #666; margin-bottom: 4px;");
    layout->addWidget(descLabel);

    auto* list = new QListWidget(&dlg);
    list->setMinimumWidth(240);
    list->setMinimumHeight(120);
    for (const auto& a : oldAliases) {
        auto* item = new QListWidgetItem(a);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        list->addItem(item);
    }
    layout->addWidget(list);

    auto* btnRow   = new QHBoxLayout();
    auto* style_ = QApplication::style();
    auto* addBtn   = new QPushButton(style_->standardIcon(QStyle::SP_FileDialogNewFolder), "", &dlg);
    addBtn->setToolTip(tr("Add alias"));
    addBtn->setFixedSize(24, 24);
    auto* removeBtn = new QPushButton(style_->standardIcon(QStyle::SP_DialogDiscardButton), "", &dlg);
    removeBtn->setToolTip(tr("Remove selected alias"));
    removeBtn->setFixedSize(24, 24);
    removeBtn->setEnabled(false);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(bbox);

    connect(list, &QListWidget::currentRowChanged, &dlg, [removeBtn, list](int row) {
        removeBtn->setEnabled(row >= 0 && list->count() > 0);
    });

    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        // Collect current names in the dialog list.
        QStringList current;
        for (int i = 0; i < list->count(); ++i)
            current << list->item(i)->text();
        // Generate a unique suffixed name.
        QString aliasName;
        for (int i = 1; ; ++i) {
            QString candidate = canonicalName + "_" + QString::number(i);
            if (!current.contains(candidate)) { aliasName = candidate; break; }
        }
        auto* item = new QListWidgetItem(aliasName);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        list->addItem(item);
        list->editItem(item);
    });

    connect(removeBtn, &QPushButton::clicked, &dlg, [list]() {
        delete list->takeItem(list->currentRow());
    });

    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QStringList newAliases;
    for (int i = 0; i < list->count(); ++i) {
        const QString a = list->item(i)->text().trimmed();
        if (!a.isEmpty()) newAliases << a;
    }
    if (newAliases == oldAliases) return;

    sprite->aliases = newAliases;
    updateAliasesButton();

    m_undoStack->push(new SetSpriteNamesCommand(
        sprite,
        canonicalName, oldAliases,
        canonicalName, newAliases,
        [this, sprite]() {
            if (m_session && m_session->selectedSprite == sprite)
                updateAliasesButton();
        }
    ));
}

void MainWindow::onProfileChanged() {
    const QString requestedProfile = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    if (requestedProfile == m_currentProfile) return;

    m_profilesTriedForCurrentLoad.clear();

    QString oldProfile = m_currentProfile;
    m_currentProfile = requestedProfile;

    m_undoStack->push(new SetProfileCommand(
        m_profileCombo,
        oldProfile,
        requestedProfile,
        [this]() {
            m_currentProfile = m_profileCombo->currentData().toString();
            scheduleLayoutRebuild(true);
        }
    ));

    // Profile changes should rebuild layout immediately - user expects visual feedback
    scheduleLayoutRebuild(true);
}

void MainWindow::captureOldSpritePositions() {
    m_oldSpritePositions.clear();
    m_oldSpritePackedRects.clear();
    m_oldSpriteRotated.clear();
    if (!m_canvas) return;

    for (const auto& model : m_session->layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite) continue;
            auto pos = m_canvas->spriteItemScenePos(sprite->path);
            if (pos.has_value()) {
                m_oldSpritePositions[sprite->path]   = pos.value();
                m_oldSpritePackedRects[sprite->path] = sprite->rect;
                m_oldSpriteRotated[sprite->path]     = sprite->rotated;
            }
        }
    }
}

void MainWindow::animateSpritesToNewPositions(
    const QMap<QString, QPointF>& newPositions,
    const QVector<QRectF>& newAtlasRects,
    const QVector<LayoutModel>& newModels,
    std::function<void()> onFinished)
{
    if (!m_enableSpriteAnimation || m_oldSpritePositions.isEmpty() || !m_canvas) {
        m_oldSpritePositions.clear();
        m_oldSpritePackedRects.clear();
        m_oldSpriteRotated.clear();
        onFinished();
        return;
    }

    // If a previous animation is still running, stop it cleanly before starting a new one.
    // Without this, two QVariantAnimations would fight over the same sprite positions each tick.
    if (m_spriteAnimation) {
        m_spriteAnimation->stop();
        // Restore labels that were hidden by the old animation.
        for (const auto& path : m_spriteAnimationPaths)
            m_canvas->setSpriteItemLabelHidden(path, false);
        m_spriteAnimationPaths.clear();
        delete m_spriteAnimation.data(); // QPointer auto-zeroes after delete
        // Re-capture positions now that the stopped animation is no longer moving sprites,
        // so the new animation starts from wherever the sprites actually are.
        captureOldSpritePositions();
    }

    // Build a path → new sprite lookup for rotation/size info.
    QMap<QString, SpritePtr> newSpriteLookup;
    for (const auto& model : newModels)
        for (const auto& sprite : model.sprites)
            if (sprite) newSpriteLookup[sprite->path] = sprite;

    // Build sprite moves.
    // For sprites that change rotation state, setPos must be adjusted so the
    // visual top-left (not the item origin) ends up exactly at the new atlas
    // position after the rotation transform is applied.
    //
    // With transform origin at the item centre (iw/2, ih/2) and rotation angle R:
    //   visual_top_left = setPos + ((iw−ih)/2, (ih−iw)/2)   [for CW  90°]
    //                   = setPos + ((iw−ih)/2, (ih−iw)/2)   [for CCW 90°, same formula]
    // Setting visual_top_left = rawNewPos gives:
    //   adjustedPos = rawNewPos + ((ih−iw)/2, (iw−ih)/2)
    struct Move {
        QString path;
        QPointF oldPos;
        QPointF newPos;       // adjusted setPos target (accounts for rotation pivot)
        qreal   endAngle;     // 0 = translate only; ±90 = rotation change
        QPointF transformOrigin; // item-space pivot (used only when endAngle != 0)
    };
    QVector<Move> moves;

    for (auto it = newPositions.begin(); it != newPositions.end(); ++it) {
        const QString& path = it.key();
        if (!m_oldSpritePositions.contains(path)) continue;

        const QPointF oldPos    = m_oldSpritePositions[path];
        const QPointF rawNewPos = it.value();
        const bool    oldRotated = m_oldSpriteRotated.value(path, false);
        const QRect   oldRect    = m_oldSpritePackedRects.value(path);
        const int     iw = oldRect.width();   // item width  on screen (packed dims)
        const int     ih = oldRect.height();  // item height on screen (packed dims)

        const SpritePtr newSprite = newSpriteLookup.value(path);
        const bool newRotated = newSprite ? newSprite->rotated : oldRotated;

        qreal   endAngle = 0.0;
        QPointF adjustedNewPos = rawNewPos;
        QPointF origin;

        if (oldRotated != newRotated && iw > 0 && ih > 0) {
            // Rotation changes: animate the scene rotation and adjust setPos.
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

    // Build atlas background rect moves.
    struct AtlasMove { int index; QRectF oldRect; QRectF newRect; };
    QVector<AtlasMove> atlasMoves;
    const QVector<QRectF> oldAtlasRects = m_canvas->currentAtlasRects();
    const int atlasCount = qMin(oldAtlasRects.size(), newAtlasRects.size());
    for (int i = 0; i < atlasCount; ++i) {
        const QRectF& o = oldAtlasRects[i];
        const QRectF& n = newAtlasRects[i];
        if (qAbs(o.x()-n.x())>0.5 || qAbs(o.y()-n.y())>0.5 ||
            qAbs(o.width()-n.width())>0.5 || qAbs(o.height()-n.height())>0.5)
            atlasMoves.append({i, o, n});
    }

    // Compute old and new scene rects so the view's scrollable area and alignment
    // transition smoothly rather than jumping when setModels fires.
    // New scene rect mirrors setModels: width = max atlas width, height = bottom of last atlas.
    const QRectF oldSceneRect = m_canvas->scene()->sceneRect();
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

    // Set transform origin points once, before the animation starts.
    for (const auto& move : moves)
        if (qAbs(move.endAngle) > 0.01)
            m_canvas->setSpriteItemTransformOrigin(move.path, move.transformOrigin);

    // Hide labels during animation to avoid visual artifacts (especially during rotation).
    // Track the paths so we can restore them if this animation is stopped early.
    m_spriteAnimationPaths.clear();
    for (const auto& move : moves) {
        m_canvas->setSpriteItemLabelHidden(move.path, true);
        m_spriteAnimationPaths.append(move.path);
    }

    // Single QVariantAnimation drives everything in lockstep:
    //   • sprite items + their border outlines (via setSpriteItemScenePos)
    //   • sprite scene rotation (via setSpriteItemRotation)
    //   • atlas background rects (via setAtlasRect)
    //   • scene rect (controls view alignment / scrollbar range)
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);

    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, moves, atlasMoves, oldSceneRect, newSceneRect](const QVariant& v) {
        const double t = v.toDouble();
        for (const auto& move : moves) {
            m_canvas->setSpriteItemScenePos(move.path,
                move.oldPos + t * (move.newPos - move.oldPos));
            if (qAbs(move.endAngle) > 0.01)
                m_canvas->setSpriteItemRotation(move.path, t * move.endAngle);
        }
        for (const auto& am : atlasMoves)
            m_canvas->setAtlasRect(am.index, QRectF(
                am.oldRect.x()      + t * (am.newRect.x()      - am.oldRect.x()),
                am.oldRect.y()      + t * (am.newRect.y()      - am.oldRect.y()),
                am.oldRect.width()  + t * (am.newRect.width()  - am.oldRect.width()),
                am.oldRect.height() + t * (am.newRect.height() - am.oldRect.height())));
        m_canvas->scene()->setSceneRect(QRectF(
            0, 0,
            oldSceneRect.width()  + t * (newSceneRect.width()  - oldSceneRect.width()),
            oldSceneRect.height() + t * (newSceneRect.height() - oldSceneRect.height())));
        m_canvas->viewport()->update();
    });

    connect(anim, &QVariantAnimation::finished, this,
            [this, moves, atlasMoves, newSceneRect, onFinished]() {
        for (const auto& move : moves) {
            m_canvas->setSpriteItemScenePos(move.path, move.newPos);
            if (qAbs(move.endAngle) > 0.01)
                m_canvas->setSpriteItemRotation(move.path, move.endAngle);
            // Show labels again after animation completes
            m_canvas->setSpriteItemLabelHidden(move.path, false);
        }
        for (const auto& am : atlasMoves)
            m_canvas->setAtlasRect(am.index, am.newRect);
        m_canvas->scene()->setSceneRect(newSceneRect);
        m_canvas->viewport()->update();
        m_spriteAnimationPaths.clear();
        onFinished();
    });

    connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
    m_spriteAnimation = anim; // QPointer auto-zeroes when anim is deleted on finish
}

void MainWindow::pauseLayoutRebuild() {
    if (m_layoutDebounceTimer && m_layoutDebounceTimer->isActive()) {
        m_layoutDebounceTimer->stop();
        m_layoutRebuildPaused = true;
    }
}

void MainWindow::resumeLayoutRebuild() {
    if (m_layoutRebuildPaused && m_layoutDirty && !m_layoutRunner) {
        // Restart the debounce timer to rebuild after user stops hovering
        scheduleLayoutRebuild();
    }
    m_layoutRebuildPaused = false;
}

void MainWindow::scheduleLayoutRebuild(bool immediate, bool skipCapture) {
    // Capture current sprite positions before layout rebuild for animation
    // (skip if already captured, e.g., during sprite removal)
    if (!skipCapture) {
        captureOldSpritePositions();
    }

    ++m_pendingChangeCount;
    refreshSpriteTree();

    // Cancel any in-progress rebuild safely (background thread sees atomic flag and kills process)
    const bool wasRunning = m_layoutRunner && m_layoutRunner->isRunning();
    if (wasRunning) {
        m_layoutRunner->stop();
        m_layoutRunPending = false;    // discard — timer will schedule a fresh run
        m_layoutRunPendingQuiet = false;
    }

    const bool bufferFull = (m_pendingChangeCount >= AppConstants::kLayoutBufferFullThreshold);

    if (bufferFull) {
        m_pendingChangeCount = 0;
        if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
        // Immediate rebuild without loading overlay
        onRunLayout(true);
    } else {
#ifdef Q_OS_WASM
        // In WASM the layout runner is synchronous and fast (no subprocess).
        // QTimer callbacks rely on Qt's RAF-driven event loop, which stops advancing
        // once pending paint events are exhausted — so a 2-second debounce timer
        // simply never fires without user input (e.g. mouse movement).
        // Run the layout immediately instead of deferring.
        if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
        m_layoutDirty = false;
        if (m_statusLabel)
            m_statusLabel->setText(tr("Layout pending rebuild..."));
        onRunLayout(true);
#else
        if (immediate) {
            if (m_layoutDebounceTimer) m_layoutDebounceTimer->stop();
            m_layoutDirty = false;
            if (m_statusLabel)
                m_statusLabel->setText(tr("Layout pending rebuild..."));
            onRunLayout(true);
        } else {
            if (!m_layoutDebounceTimer) {
                m_layoutDebounceTimer = new QTimer(this);
                m_layoutDebounceTimer->setSingleShot(true);
                connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
                    if (m_atlasViewStack && m_atlasViewStack->currentIndex() == 1) {
                        // Navigator is active — defer until the user switches to Layout
                        m_layoutDirty = true;
                    } else {
                        m_layoutDirty = false;
                        onRunLayout(true);   // quiet rebuild from timer
                    }
                });
            }
            // Only start the timer if rebuild is not paused (user hovering over canvas)
            if (!m_layoutRebuildPaused) {
                m_layoutDebounceTimer->start(AppConstants::kLayoutDebounceMs);
            } else {
                // Mark as dirty so it resumes after user stops hovering
                m_layoutDirty = true;
            }
            if (m_statusLabel)
                m_statusLabel->setText(tr("Layout pending rebuild..."));
        }
#endif
    }
}

void MainWindow::onLayoutZoomChanged(double value) {
    if (m_canvas && !m_layoutZoomSpin->signalsBlocked()) {
        m_canvas->setZoomManual(true);
    }
    if (m_canvas) {
        m_canvas->setZoom(value / 100.0);
    }
}

void MainWindow::handleDimensionsError(const QString& failedProfile) {
    const QVector<SpratProfile> allProfiles = configuredProfiles();

    // Determine failed profile's properties
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

    // Pass 1: prefer multipack=true if failed profile had multipack=false
    if (!failedHasMultipack) {
        for (int i = 0; i < m_profileCombo->count() && fallback.isEmpty(); ++i) {
            const QString c = m_profileCombo->itemData(i).toString();
            if (c == failedProfile || !isProfileEnabled(c)) continue;
            if (const SpratProfile* d = profileDef(c); d && d->multipack)
                fallback = c;
        }
    }

    // Pass 2: prefer profile with larger atlas area
    if (fallback.isEmpty()) {
        int bestArea = failedArea;
        for (int i = 0; i < m_profileCombo->count(); ++i) {
            const QString c = m_profileCombo->itemData(i).toString();
            if (c == failedProfile || !isProfileEnabled(c)) continue;
            if (const SpratProfile* d = profileDef(c)) {
                int area = d->maxWidth * d->maxHeight;
                if (area > bestArea) { bestArea = area; fallback = c; }
            }
        }
    }

    // Pass 3: any other enabled profile
    if (fallback.isEmpty()) {
        for (int i = 0; i < m_profileCombo->count() && fallback.isEmpty(); ++i) {
            const QString c = m_profileCombo->itemData(i).toString();
            if (c != failedProfile && isProfileEnabled(c)) fallback = c;
        }
    }

    if (fallback.isEmpty()) {
        m_statusLabel->setText(tr("Error running layout"));
        MessageDialog::critical(this, tr("Error"),
            tr("Sprite dimensions exceed atlas limits and no suitable fallback profile is available."));
        m_layoutFailureDialogShown = true;
    } else {
    }

    m_statusLabel->setText(
        tr("Profile '%1' failed (sprite too large), retrying with '%2'...")
            .arg(failedProfile, fallback));

    if (m_profileCombo->currentData().toString() == fallback)
        onRunLayout();
    else
        m_profileCombo->setCurrentIndex(m_profileCombo->findData(fallback));
}
