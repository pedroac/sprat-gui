#include "MainWindow.h"

#include "SpriteSelectionPresenter.h"
#include "LayoutRunner.h"
#include "LayoutParser.h"
#include "ResolutionUtils.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
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

namespace {
int legacyDefaultPivotX(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.width() / 2) : 0;
}

int legacyDefaultPivotY(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.height() / 2) : 0;
}
}

void MainWindow::onRunLayout() {
    // If sync is active, ensure active frames have been copied to the sprites folder.
    if (m_settings.syncMode != SyncMode::None
        && m_session
        && !m_session->sourceFolder.isEmpty()
        && !m_session->activeFramePaths.isEmpty()
        && !activeFramesAreInSourceFolder()) {
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        if (m_session->layoutSourceIsList) {
            ensureFrameListInput();
        }
    }

    if (m_session->layoutSourcePath.isEmpty()) {
        return;
    }
    if (!m_cliReady) {
        checkCliTools();
        return;
    }
    if (m_layoutRunner && m_layoutRunner->isRunning()) {
        m_layoutRunner->stop();
    }

    const QString requestedProfile = m_profileCombo->currentText().trimmed();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);

    LayoutRunConfig config;
    config.sourcePath = m_session->layoutSourcePath;
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
    m_loadingUiMessage = tr("Building layout...");
    m_statusLabel->setText(tr("Running spratlayout..."));
    m_isCanceled = false;
    setLoading(true);
    m_layoutFailureDialogShown = false;
    
    qInfo() << "[Layout] Dispatching run, sourcePath=" << config.sourcePath
            << "exists=" << QFile::exists(config.sourcePath)
            << "isList=" << m_session->layoutSourceIsList;
    m_layoutRunner->run(config);
}

void MainWindow::onLayoutFinished(const LayoutResult& result) {
    if (!result.success) {
        setLoading(false);

        const QString combined = (result.error + "\n" + result.output).toLower();

        if (combined.contains("sprite dimensions exceed")) {
            handleDimensionsError(m_runningLayoutProfile);
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

        QString details = result.error;
        if (details.isEmpty()) details = result.output;
        if (details.isEmpty()) details = tr("spratlayout exited with code %1.").arg(result.exitCode);

        m_statusLabel->setText(tr("Error running layout"));
        qCritical() << "spratlayout process failed. Exit code:" << result.exitCode << "Error:" << result.error << "Output:" << result.output;

        // Don't show error dialog if it was retrying or if it was explicitly stopped (which often results in non-zero exit code but we don't want a dialog)
        if (!m_layoutFailureDialogShown && !result.error.contains("stopped", Qt::CaseInsensitive)) {
            // Check if it was killed by us (no output and no error usually means killed)
            if (!result.output.isEmpty() || !result.error.isEmpty()) {
                QMessageBox::critical(this, tr("Error"), tr("spratlayout failed:\n") + details);
                m_layoutFailureDialogShown = true;
            }
        }
        
        m_retryWithoutTrimOnFailure = false;
        if (m_layoutRunPending) {
            m_layoutRunPending = false;
            onRunLayout();
        }
        return;
    }

    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;

    const QString layoutText = result.output;
    QElapsedTimer parseTimer;
    parseTimer.start();
    QVector<LayoutModel> newModels = LayoutParser::parse(layoutText, layoutParserFolder());
    qInfo() << "[WASM] LayoutParser::parse done"
            << "models=" << newModels.size()
            << "ms=" << parseTimer.elapsed();
    if (newModels.isEmpty()) {
        newModels.append(LayoutModel());
    }

    m_session->cachedLayoutOutput = layoutText;
    m_session->cachedLayoutScale = newModels.first().scale;

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
                s->name = oldS->name;
                const bool oldPivotIsLegacyDefault =
                    oldS->pivotX == legacyDefaultPivotX(oldS) &&
                    oldS->pivotY == legacyDefaultPivotY(oldS);
                // Preserve user-edited pivots; let legacy auto pivots upgrade to the new default.
                if (!oldPivotIsLegacyDefault) {
                    s->pivotX = oldS->pivotX;
                    s->pivotY = oldS->pivotY;
                }
                s->points = oldS->points;
            }
        }
    }

    QStringList selectedPaths;
    for (const auto& s : m_session->selectedSprites) {
        selectedPaths << s->path;
    }
    const QString primaryPath = m_session->selectedSprite ? m_session->selectedSprite->path : QString();

    m_session->layoutModels = newModels;

    m_loadingUiMessage = tr("Loading images...");
    setLoading(true);

    qInfo() << "[WASM] setModelsAsync start"
            << "models=" << m_session->layoutModels.size();
    m_canvas->setModelsAsync(m_session->layoutModels, &m_isCanceled, [this, selectedPaths, primaryPath]() {
        if (m_isCanceled) {
            setLoading(false);
            m_statusLabel->setText(tr("Loading images canceled"));
            return;
        }

        // Only reset viewport when profile changes, not when sprites are added/removed/modified
        const QString currentProfile = m_profileCombo ? m_profileCombo->currentText().trimmed() : QString();
        const bool profileChanged = m_session->lastSuccessfulProfile != currentProfile;

        if (profileChanged) {
            m_canvas->setZoomManual(false);
            QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);
        }

        m_statusLabel->setText(QString(tr("Loaded %1 sprites in %2 atlas(es)"))
            .arg(m_session->activeFramePaths.size())
            .arg(m_session->layoutModels.size()));

        populateActiveFrameListFromModel();
        if (m_session->layoutSourceIsList) {
            updateManualFrameLabel();
        }

        if (!m_session->pendingProjectPayload.isEmpty()) {
            applyProjectPayload();
        } else if (!selectedPaths.isEmpty()) {
            m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
        }

        m_session->lastSuccessfulProfile = m_profileCombo->currentText();

        updateMainContentView();
        updateUiState();

        // Clear temporary folders used for ZIP extraction - they're no longer needed
        // since images have been loaded into the layout and (if sync is active)
        // copied to the sprites folder
        m_session->clearTempDirs();

        setLoading(false);
        qInfo() << "[WASM] setModelsAsync finished";

        if (m_layoutRunPending) {
            m_layoutRunPending = false;
            onRunLayout();
        }
    });
}

void MainWindow::onLayoutError(const QString& details) {
    setLoading(false);
    m_runningLayoutProfile.clear();
    m_retryWithoutTrimOnFailure = false;
    
    m_statusLabel->setText(tr("Error running layout process"));
    qCritical() << "spratlayout process error:" << details;
    
    if (!m_layoutFailureDialogShown) {
        QMessageBox::critical(this, tr("Error"), tr("spratlayout process failed:\n") + details);
        m_layoutFailureDialogShown = true;
    }
    
    if (m_layoutRunPending) {
        m_layoutRunPending = false;
        onRunLayout();
    }
}

bool MainWindow::isProfileEnabled(const QString& profile) const {
    const int index = m_profileCombo->findText(profile);
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
    const int failedIndex = m_profileCombo->findText(failedProfile);
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
            QString candidate = m_profileCombo->itemText(i);
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
        QMessageBox::warning(this, tr("Profile disabled"), tr("The selected profile failed and was disabled. No fallback profile is available."));
        return;
    }
    if (m_profileCombo->currentText() == fallbackProfile) {
        onRunLayout();
        return;
    }

    m_profileCombo->setCurrentText(fallbackProfile);
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
    } else {
        if (m_welcomeLabel) {
            m_welcomeLabel->setText(tr("Drag and drop a folder, image file, archive (zip/tar), or URL"));
        }
        if (!m_cliInstallInProgress && m_cliInstallOverlay) {
            m_cliInstallOverlay->hide();
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            m_loadingOverlayVisible = false;
        }
        m_loadingUiMessage = tr("Loading...");
    }
    if (m_statusProgressBar) {
        m_statusProgressBar->setVisible(loading);
    }
    setCursor(loading ? Qt::WaitCursor : Qt::ArrowCursor);
    updateUiState();
}

void MainWindow::onSpriteSelected(SpritePtr sprite) {
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
}

void MainWindow::onProfileChanged() {
    const QString requestedProfile = m_profileCombo ? m_profileCombo->currentText().trimmed() : QString();
    onRunLayout();
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
            const QString c = m_profileCombo->itemText(i).trimmed();
            if (c == failedProfile || !isProfileEnabled(c)) continue;
            if (const SpratProfile* d = profileDef(c); d && d->multipack)
                fallback = c;
        }
    }

    // Pass 2: prefer profile with larger atlas area
    if (fallback.isEmpty()) {
        int bestArea = failedArea;
        for (int i = 0; i < m_profileCombo->count(); ++i) {
            const QString c = m_profileCombo->itemText(i).trimmed();
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
            const QString c = m_profileCombo->itemText(i).trimmed();
            if (c != failedProfile && isProfileEnabled(c)) fallback = c;
        }
    }

    if (fallback.isEmpty()) {
        m_statusLabel->setText(tr("Error running layout"));
        QMessageBox::critical(this, tr("Error"),
            tr("Sprite dimensions exceed atlas limits and no suitable fallback profile is available."));
        m_layoutFailureDialogShown = true;
        return;
    }

    m_statusLabel->setText(
        tr("Profile '%1' failed (sprite too large), retrying with '%2'...")
            .arg(failedProfile, fallback));

    if (m_profileCombo->currentText().trimmed() == fallback)
        onRunLayout();
    else
        m_profileCombo->setCurrentText(fallback);
}
