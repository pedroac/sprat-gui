#include "MainWindow.h"

#include "SpriteSelectionPresenter.h"
#include "LayoutRunner.h"
#include "LayoutParser.h"
#include "ResolutionUtils.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>

namespace {
int legacyDefaultPivotX(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.width() / 2) : 0;
}

int legacyDefaultPivotY(const SpritePtr& sprite) {
    return sprite ? (sprite->rect.height() / 2) : 0;
}
}

void MainWindow::onRunLayout() {
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

    m_session->lastRunUsedTrim = (hasSelectedProfile ? selectedProfile.trimTransparent : false) && !m_retryWithoutTrimOnFailure;
    m_runningLayoutProfile.clear();
    m_statusLabel->setText(tr("Running spratlayout..."));
    setLoading(true);
    m_layoutFailureDialogShown = false;
    
    m_layoutRunner->run(config);
}

void MainWindow::onLayoutFinished(const LayoutResult& result) {
    setLoading(false);

    if (!result.success) {
        m_runningLayoutProfile.clear();
        
        const QString combined = (result.error + "\n" + result.output).toLower();
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
    QVector<LayoutModel> newModels = LayoutParser::parse(layoutText, layoutParserFolder());
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

    m_canvas->setModels(m_session->layoutModels);
    m_canvas->setZoomManual(false);
    QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);

    m_statusLabel->setText(QString(tr("Loaded %1 sprites in %2 atlas(es)"))
        .arg(m_session->activeFramePaths.size())
        .arg(newModels.size()));

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
    if (m_layoutRunPending) {
        m_layoutRunPending = false;
        onRunLayout();
    }
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
        m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
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
            m_welcomeLabel->setText(tr("Drag and Drop folder with image files"));
        }
        if (!m_cliInstallInProgress && m_cliInstallOverlay) {
            if (m_cliInstallProgress) {
                m_cliInstallProgress->show();
            }
            m_cliInstallOverlay->hide();
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            m_loadingOverlayVisible = false;
        }
        m_loadingUiMessage = tr("Loading...");
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
