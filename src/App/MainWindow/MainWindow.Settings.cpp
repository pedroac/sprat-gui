/// @file MainWindow.Settings.cpp
/// Settings and profiles: opening the settings dialog on a section, applying settings, and the configured layout profiles.

#include "MainWindow.h"
#include "MessageText.h"
#include "NineSliceWorkspace.h"
#include "PuppetWorkspace.h"
#include "AnimationPreviewPanel.h"
#include "ExportWorkspace.h"
#include "FrameAnimationWorkspace.h"
#include "SpriteEditorPanel.h"
#include "NavigatorTreeWidget.h"
#include "LayoutOrchestrator.h"
#include "CliSetupController.h"
#include "ProjectController.h"
#include "PackedAtlasView.h"
#include "AtlasesManagementWorkspace.h"
#include "UndoCommands.h"
#include "AnimationCanvas.h"
#include "MarkersDialog.h"
#include "LayoutCanvas.h"
#include "NavigatorPanel.h"
#include "SettingsDialog.h"
#include "CliToolsConfig.h"
#include "LayoutParser.h"
#include "ProjectPayloadCodec.h"
#include "AutosaveProjectStore.h"
#include "AnimationTimelineOps.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationExportService.h"
#include "SpriteSelectionPresenter.h"
#include "CliToolsUi.h"
#include "MainWindowUiState.h"
#include "SettingsCoordinator.h"
#include "ProjectFileLoader.h"
#include "TimelineGenerationService.h"
#include "ProjectSaveService.h"
#include "TimelineUi.h"
#include "ProfilesDialog.h"
#include "MessageDialog.h"
#include "SpratProfilesConfig.h"
#include "FolderSyncService.h"
#include "SpriteNameUtils.h"
#include "AppConstants.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#include "WasmFolderBrowserDialog.h"
#endif
#ifndef Q_OS_WASM
#include "UpdateChecker.h"
#include "UpdateDialog.h"
#endif
#include <algorithm>
#include <memory>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QStackedWidget>
#include <QResizeEvent>
#include <QProgressBar>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QIODevice>
#include <QUuid>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QThread>
#include <QSpinBox>
#include <QUndoStack>
#include <QShortcut>
#include <QToolButton>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QPainter>
#include <QImage>
#include <QDirIterator>
#include "ArchiveExtractor.h"
#include <QAtomicPointer>

void MainWindow::openSettingsDialogForSection(SettingsDialog::Section section) {
    SettingsDialog dlg(m_settings, m_cliPaths, this, section);
    QObject::connect(&dlg, &SettingsDialog::installCliToolsRequested, this, &MainWindow::installCliTools);
    QObject::connect(&dlg, &SettingsDialog::syncNowRequested, this, &MainWindow::onSyncNowRequested);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.getSettings();
        m_cliPaths = dlg.getCliPaths();
        if (m_cliSetup) m_cliSetup->setCliBaseDir(m_cliPaths.baseDir);
        applySettings();
        CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
        checkCliTools();
    }
}

QVector<SpratProfile> MainWindow::configuredProfiles() {
    QString error;
    const QVector<SpratProfile> profiles = SpratProfilesConfig::loadProfileDefinitions(&error);
    if (!error.isEmpty()) {
        m_statusLabel->setText(tr("Invalid profiles configuration"));
        MessageDialog::warning(this, tr("Profiles"), MessageText::withToolOutput(tr("The profiles configuration couldn't be loaded, so no layout profiles are available:"), error)
                                   + QStringLiteral("\n\n") + tr("Fix or remove the file named above, then restart sprat-gui."));
    }
    return profiles;
}

bool MainWindow::selectedProfileDefinition(SpratProfile& out) const {
    auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
    if (!profileCombo) {
        return false;
    }
    const QString selectedName = profileCombo->currentData().toString().trimmed();
    if (selectedName.isEmpty()) {
        return false;
    }
    const QVector<SpratProfile> profiles = SpratProfilesConfig::loadProfileDefinitions();
    for (const SpratProfile& profile : profiles) {
        if (profile.name.trimmed() == selectedName) {
            out = profile;
            return true;
        }
    }
    return false;
}

void MainWindow::applyConfiguredProfiles(const QVector<SpratProfile>& profiles, const QString& preferred) {
    auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
    if (!profileCombo) {
        return;
    }

    QStringList effectiveNames;
    for (const SpratProfile& profile : profiles) {
        const QString trimmed = profile.name.trimmed();
        if (trimmed.isEmpty() || effectiveNames.contains(trimmed)) {
            continue;
        }
        effectiveNames.append(trimmed);
    }
    const QString previousSelected = profileCombo->currentData().toString().trimmed();
    profileCombo->blockSignals(true);
    profileCombo->clear();
    for (const SpratProfile& profile : profiles) {
        const QString name = profile.name.trimmed();
        if (name.isEmpty() || profileCombo->findData(name) >= 0) {
            continue;
        }
        const QString display = profile.label.trimmed().isEmpty() ? name : profile.label.trimmed();
        profileCombo->addItem(display, name);
    }
    if (!effectiveNames.isEmpty()) {
        QString selected = preferred.trimmed();
        if (selected.isEmpty()) {
            selected = previousSelected;
        }
        if (selected.isEmpty() || !effectiveNames.contains(selected)) {
            selected = effectiveNames.first();
        }
        const int idx = profileCombo->findData(selected);
        profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    profileCombo->blockSignals(false);

    if (m_profileSelectorStack) {
        m_profileSelectorStack->setCurrentIndex(effectiveNames.isEmpty() ? 1 : 0);
    }
    m_currentProfile = profileCombo->currentData().toString();

    if (!effectiveNames.contains(m_session->lastSuccessfulProfile)) {
        m_session->lastSuccessfulProfile.clear();
    }

    QStringList profileNames, profileLabels;
    for (const SpratProfile& p : profiles) {
        const QString name = p.name.trimmed();
        if (!name.isEmpty() && !profileNames.contains(name)) {
            profileNames  << name;
            profileLabels << (p.label.trimmed().isEmpty() ? name : p.label.trimmed());
        }
    }

    if (m_atlasesManagementWorkspace) {
        const QStringList prevEnabled = m_atlasesManagementWorkspace->enabledProfiles();
        m_atlasesManagementWorkspace->setProfiles(profileNames, profileLabels, m_currentProfile, prevEnabled);
        m_atlasesManagementWorkspace->setProfilesGlobal(m_lastSaveConfig.profilesGlobal);
    }
}

/**
 * @brief Opens the settings dialog.
 */
void MainWindow::onSettingsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::FramesEditor);
}

void MainWindow::onSettingsSpritesheetClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Spritesheet);
}

void MainWindow::onSettingsSpritesNavigatorClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::SpritesNavigator);
}

void MainWindow::onSettingsFramesEditorClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::FramesEditor);
}

void MainWindow::onSettingsAtlasLayoutClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::AtlasLayout);
}

void MainWindow::onSettingsExportationClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Exportation);
}

void MainWindow::onSettingsNineSliceClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::NineSlice);
}

#ifndef Q_OS_WASM
void MainWindow::onSettingsCliToolsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::CliTools);
}
#endif

void MainWindow::onManageProfiles() {
    ProfilesDialog dialog(configuredProfiles(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QVector<SpratProfile> profiles = dialog.profiles();
    if (!SpratProfilesConfig::saveProfileDefinitions(profiles)) {
        MessageDialog::warning(this, tr("Profiles"), tr("Could not save profiles configuration."));
        return;
    }

    auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
    const QString previousProfile = profileCombo ? profileCombo->currentData().toString() : QString();
    applyConfiguredProfiles(profiles, previousProfile);

    // Invalidate cached layout so changed profile settings take effect
    if (m_session) {
        m_session->cachedLayoutOutput.clear();
        m_session->lastSuccessfulProfile.clear();
    }

    if (m_exportWorkspace && m_currentWorkspace == m_exportWorkspace) {
        // Re-populate the export workspace profile combo with the updated definitions
        const QStringList enabled = m_atlasesManagementWorkspace
            ? m_atlasesManagementWorkspace->enabledProfiles() : QStringList();
        QVector<SpratProfile> toExport;
        for (const SpratProfile& p : profiles) {
            if (enabled.isEmpty() || enabled.contains(p.name.trimmed()))
                toExport << p;
        }
        m_exportWorkspace->populate(toExport,
                                    profileCombo ? profileCombo->currentData().toString() : QString(),
                                    m_lastSaveConfig,
                                    QString());
        // Refresh the preview with the updated profile settings
        const SaveConfig cfg = m_exportWorkspace->getConfig();
        refreshPreview(cfg.profiles.isEmpty() ? QString() : cfg.profiles.first(),
                       cfg.scaleFilter);
    } else if (!m_session->layoutSourcePath.isEmpty()
               && profileCombo
               && !profileCombo->currentData().toString().trimmed().isEmpty()) {
        // Rebuild the atlas layout with the new profile settings
        scheduleLayoutRebuild(true);
    }
}

void MainWindow::updateOnionSkinDisplay() {
    auto* previewCanvas = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
    if (!previewCanvas || !m_session) return;
    auto* spriteEditorPanel = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel() : nullptr;
    const bool enabled = spriteEditorPanel && spriteEditorPanel->onionSkinBtn()->isChecked();
    if (!enabled || m_settings.onionSkinOpacity == 0) {
        previewCanvas->setGhostSprites({});
        return;
    }
    const QPoint activePivot = m_session->selectedSprite
        ? QPoint(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY)
        : QPoint();
    QList<SpritePtr> ghosts;
    for (const auto& s : m_session->selectedSprites)
        if (s && s != m_session->selectedSprite) ghosts.append(s);
    previewCanvas->setGhostSprites(ghosts, activePivot);
}

/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
    auto* previewCanvas = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
    auto* apS = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
    auto* animCanvas = apS ? apS->animCanvas() : nullptr;
    if (canvas) {
        SettingsCoordinator::apply(m_settings, canvas, previewCanvas, animCanvas);
    }
    if (m_packedAtlasView) {
        m_packedAtlasView->setSettings(m_settings);
    }
    if (m_exportLayoutCanvas) {
        m_exportLayoutCanvas->setSettings(m_settings);
    }
    if (m_nineSliceWorkspace) {
        m_nineSliceWorkspace->setSettings(m_settings);
    }
    if (m_puppetWorkspace) {
        m_puppetWorkspace->canvas()->setBackgroundBrush(m_settings.workspaceColor);
    }
    auto* navigatorPanel = m_atlasWorkspace ? m_atlasWorkspace->navigatorPanel() : nullptr;
    auto* spriteTree = navigatorPanel ? navigatorPanel->tree() : nullptr;
    if (spriteTree) {
        spriteTree->setPreviewEnabled(m_settings.spritePreviewEnabled);
        spriteTree->setPreviewDelay(static_cast<int>(m_settings.spritePreviewDelay * 1000.0));
    }
    if (navigatorPanel) {
        navigatorPanel->setGroupSimilar(m_settings.navigatorGroupSimilar);
        refreshSpriteTree();
    }

    // Update source folder watcher based on sync mode
    if (m_settings.syncMode == SyncMode::None) {
        cleanupSourceFolderWatcher();
    } else {
        // For all non-None modes (Manual and Watch), call initializeSourceFolderWatcher()
        // which handles the mode internally via its own if branches
        initializeSourceFolderWatcher();

        // Stop polling timer — QFileSystemWatcher handles changes event-driven
        if (m_watchModePeriodicCheckTimer) {
            m_watchModePeriodicCheckTimer->stop();
        }
    }

    // If sync was just enabled (None -> active) and a layout already exists,
    // re-run layout to copy images into the sprites folder.
    if (m_settings.syncMode != SyncMode::None
        && m_appliedSyncMode == SyncMode::None
        && m_session
        && !m_session->activeAtlas().layoutModels.isEmpty()
        && m_atlasWorkspace && m_atlasWorkspace->canvas()) {
        // Sync mode enabled - rebuild immediately
        scheduleLayoutRebuild(true);
    }
    m_appliedSyncMode = m_settings.syncMode;

    // Propagate settings changes to LayoutOrchestrator
    if (m_layoutOrchestrator) {
        m_layoutOrchestrator->setSyncMode(m_settings.syncMode);
        m_layoutOrchestrator->setDeduplicateMode(m_settings.deduplicateMode);
        m_layoutOrchestrator->setDedupThreshold(m_settings.dedupThreshold);
        m_layoutOrchestrator->setIncrementalLayout(m_settings.incrementalLayout);
        m_layoutOrchestrator->setLayoutZoomOnChange(m_settings.layoutZoomOnChange);
    }

    // Propagate settings changes to ExportCoordinator
    if (m_exportCoordinator) {
        m_exportCoordinator->setAppSettings(m_settings);
    }

    // Propagate frame detection settings to ProjectController
    if (m_projectController) {
        m_projectController->setFramesSettings({
            m_settings.framesHasRectangles,
            m_settings.framesRectangleColor,
            m_settings.framesTolerance,
            m_settings.framesMinSize,
            m_settings.framesMaxSprites
        });
    }

    // Refresh folder label to add/remove the "(watching)" suffix
    if (m_session && !m_session->currentFolder.isEmpty())
        updateFolderLabel(m_session->currentFolder);

    updateOpenSourceFolderAction();
    updateOnionSkinDisplay();

    // Refresh multi-selection label
    auto* multiSelectionLabel = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->multiSelectionLabel() : nullptr;
    if (multiSelectionLabel && m_session) {
        const int n = m_session->selectedSprites.size();
        if (n > 1) {
            multiSelectionLabel->setText(m_settings.propagateEditsToChecked
                ? tr("%1 sprites selected — pivot and marker changes apply to all").arg(n)
                : tr("%1 sprites selected — changes apply to current frame only").arg(n));
            multiSelectionLabel->setVisible(true);
        } else {
            multiSelectionLabel->setVisible(false);
        }
    }
}
