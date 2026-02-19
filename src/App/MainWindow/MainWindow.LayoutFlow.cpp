#include "MainWindow.h"

#include "SpriteSelectionPresenter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>

void MainWindow::onRunLayout() {
    if (m_layoutSourcePath.isEmpty()) {
        return;
    }
    if (!m_cliReady) {
        checkCliTools();
        return;
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_layoutRunPending = true;
        return;
    }

    const QString requestedProfile = m_profileCombo->currentText();

    QStringList args;
    args << m_layoutSourcePath;
    args << "--profile" << m_profileCombo->currentText();
    args << "--padding" << QString::number(m_paddingSpin->value());
    if (m_trimCheck->isChecked()) {
        args << "--trim-transparent";
    }
    m_runningLayoutProfile = requestedProfile;
    appendDebugLog(QString("Running spratlayout: source='%1' profile='%2' padding=%3 trim=%4")
                       .arg(m_layoutSourcePath,
                            m_profileCombo->currentText(),
                            QString::number(m_paddingSpin->value()),
                            m_trimCheck->isChecked() ? "true" : "false"));

    m_statusLabel->setText("Running spratlayout...");
    setLoading(true);
    m_process->start(m_spratLayoutBin, args);
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    setLoading(false);
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        const QString failedProfile = m_runningLayoutProfile.isEmpty() ? m_profileCombo->currentText() : m_runningLayoutProfile;
        m_runningLayoutProfile.clear();
        const QString err = m_process->readAllStandardError();
        m_statusLabel->setText("Error running layout");
        qCritical() << "spratlayout process failed. Exit code:" << exitCode << "Error:" << err;
        QMessageBox::critical(this, "Error", "spratlayout failed:\n" + err);
        handleProfileFailure(failedProfile);
        if (m_layoutRunPending) {
            m_layoutRunPending = false;
            onRunLayout();
        }
        return;
    }
    m_runningLayoutProfile.clear();

    const QByteArray processOutput = m_process->readAllStandardOutput();
    const QString layoutText = QString::fromUtf8(processOutput);
    LayoutModel newModel = parseLayoutOutput(layoutText, layoutParserFolder());
    m_cachedLayoutOutput = layoutText;
    m_cachedLayoutScale = newModel.scale;
    appendDebugLog(QString("spratlayout finished: atlas=%1x%2 scale=%3 sprites=%4 output_bytes=%5")
                       .arg(QString::number(newModel.atlasWidth),
                            QString::number(newModel.atlasHeight),
                            QString::number(newModel.scale),
                            QString::number(newModel.sprites.size()),
                            QString::number(processOutput.size())));
    if (m_pendingProjectPayload.isEmpty()) {
        QMap<QString, SpritePtr> oldSprites;
        for (const auto& s : m_layoutModel.sprites) {
            oldSprites[s->path] = s;
        }
        for (auto& s : newModel.sprites) {
            if (!oldSprites.contains(s->path)) {
                continue;
            }
            auto oldS = oldSprites[s->path];
            s->name = oldS->name;
            s->pivotX = oldS->pivotX;
            s->pivotY = oldS->pivotY;
            s->points = oldS->points;
        }
    }

    QStringList selectedPaths;
    for (const auto& s : m_selectedSprites) {
        selectedPaths << s->path;
    }
    const QString primaryPath = m_selectedSprite ? m_selectedSprite->path : QString();

    m_layoutModel = newModel;
    m_canvas->setModel(m_layoutModel);
    m_statusLabel->setText(QString("Loaded %1 sprites").arg(m_layoutModel.sprites.size()));

    populateActiveFrameListFromModel();
    if (m_layoutSourceIsList) {
        updateManualFrameLabel();
    }

    if (!m_pendingProjectPayload.isEmpty()) {
        applyProjectPayload();
    } else if (!selectedPaths.isEmpty()) {
        m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
    }

    m_lastSuccessfulProfile = m_profileCombo->currentText();

    updateMainContentView();
    updateUiState();
    if (m_layoutRunPending) {
        m_layoutRunPending = false;
        onRunLayout();
    }
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    setLoading(false);
    m_runningLayoutProfile.clear();
    if (error == QProcess::FailedToStart) {
        m_statusLabel->setText("Error: spratlayout not found");
        qCritical() << "Failed to start spratlayout process. Make sure it is installed and in your PATH.";
        QMessageBox::critical(this, "Error", "Could not start 'spratlayout'.\nMake sure it is installed and in your PATH.");
        if (m_layoutRunPending) {
            m_layoutRunPending = false;
            onRunLayout();
        }
        return;
    }
    m_statusLabel->setText("Error running layout process");
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
    if (!m_lastSuccessfulProfile.isEmpty() &&
        m_lastSuccessfulProfile != failedProfile &&
        isProfileEnabled(m_lastSuccessfulProfile)) {
        fallbackProfile = m_lastSuccessfulProfile;
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
        QMessageBox::warning(this, "Profile disabled", "The selected profile failed and was disabled. No fallback profile is available.");
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
        if (m_welcomeLabel && m_layoutModel.sprites.isEmpty()) {
            m_welcomeLabel->setText(m_loadingUiMessage);
        }
        if (m_mainStack && m_welcomePage && m_layoutModel.sprites.isEmpty()) {
            m_mainStack->setCurrentWidget(m_welcomePage);
        }
        if (!m_cliInstallInProgress && m_loadingOverlayDelayTimer) {
            if (m_forceImmediateLoadingOverlay) {
                if (m_loadingOverlayDelayTimer->isActive()) {
                    m_loadingOverlayDelayTimer->stop();
                }
                showLoadingOverlayNow();
            } else {
                m_loadingOverlayDelayTimer->start(3000);
            }
        }
    } else {
        if (m_loadingOverlayDelayTimer && m_loadingOverlayDelayTimer->isActive()) {
            m_loadingOverlayDelayTimer->stop();
        }
        if (m_welcomeLabel) {
            m_welcomeLabel->setText("Drag and Drop folder with image files");
        }
        if (!m_cliInstallInProgress && m_cliInstallOverlay) {
            if (m_cliInstallProgress) {
                m_cliInstallProgress->show();
            }
            m_cliInstallOverlay->hide();
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            m_loadingOverlayVisible = false;
        }
        m_loadingUiMessage = "Loading...";
    }
    setCursor(loading ? Qt::WaitCursor : Qt::ArrowCursor);
    updateUiState();
}

void MainWindow::onSpriteSelected(SpritePtr sprite) {
    m_selectedSprite = sprite;
    if (sprite) {
        m_statusLabel->setText("Selected: " + sprite->name);
    }
    SpriteSelectionPresenter::applySpriteSelection(
        sprite,
        m_selectedPointName,
        m_spriteNameEdit,
        m_pivotXSpin,
        m_pivotYSpin,
        m_configPointsBtn,
        m_previewView,
        m_previewZoomSpin,
        m_handleCombo);
}

void MainWindow::onProfileChanged() {
    onRunLayout();
}

void MainWindow::onPaddingChanged() {
    onRunLayout();
}

void MainWindow::onLayoutZoomChanged(double value) {
    m_canvas->setZoom(value);
}
