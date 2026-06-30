#include "MainWindow.h"
#include "LayoutCanvas.h"
#include "LayoutOrchestrator.h"
#include "AtlasesManagementWorkspace.h"
#include "ElidedLabel.h"
#include "UndoCommands.h"
#include "SpriteEditorPanel.h"

#include "SpriteSelectionPresenter.h"
#include "TimelineBuilder.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
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

// ============================================================
// Thin delegates — logic has moved to LayoutOrchestrator
// ============================================================

void MainWindow::onRunLayout(bool quiet) {
    if (m_layoutOrchestrator) m_layoutOrchestrator->run(quiet);
}

void MainWindow::scheduleLayoutRebuild(bool immediate, bool skipCapture) {
    if (m_layoutOrchestrator) m_layoutOrchestrator->schedule(immediate, skipCapture);
}

void MainWindow::pauseLayoutRebuild() {
    if (m_layoutOrchestrator) m_layoutOrchestrator->pause();
}

void MainWindow::resumeLayoutRebuild() {
    if (m_layoutOrchestrator) m_layoutOrchestrator->resume();
}

void MainWindow::captureOldSpritePositions() {
    if (m_layoutOrchestrator) m_layoutOrchestrator->capturePositions();
}

// ============================================================
// UI-layer methods that stay in MainWindow
// ============================================================

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
        if (m_welcomeLabel && (m_session->activeAtlas().layoutModels.isEmpty() || m_session->activeAtlas().layoutModels.first().sprites.isEmpty())) {
            m_welcomeLabel->setText(m_loadingUiMessage);
        }
        if (m_mainStack && m_welcomePage && (m_session->activeAtlas().layoutModels.isEmpty() || m_session->activeAtlas().layoutModels.first().sprites.isEmpty())) {
            m_mainStack->setCurrentWidget(m_welcomePage);
        }
        if (!m_cliInstallInProgress) {
            showLoadingOverlayNow();
        }
        // Disable dockers and canvas while loading - prevent interaction with stale data
        {
            auto* canvas = m_atlasWorkspace->canvas();
            if (canvas) {
                canvas->setEnabled(false);
                // Create semi-transparent overlay if not already created
                if (!m_canvasOverlay) {
                    m_canvasOverlay = new QWidget(canvas);
                    m_canvasOverlay->setStyleSheet("background-color: rgba(128, 128, 128, 160);");
                }
                // Show overlay and resize to match canvas
                m_canvasOverlay->resize(canvas->size());
                m_canvasOverlay->raise();
                m_canvasOverlay->show();
            }
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
        {
            auto* canvas = m_atlasWorkspace->canvas();
            if (canvas) {
                canvas->setEnabled(true);
                // Hide overlay to restore normal canvas appearance
                if (m_canvasOverlay) {
                    m_canvasOverlay->hide();
                }
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
    m_atlasWorkspace->clearCoordinateOverride();

    auto* spriteEditorPanel = m_atlasWorkspace->spriteEditorPanel();
    auto* previewView = spriteEditorPanel->previewCanvas();

    // Flipbook: capture the previous pivot's screen position before any state changes.
    QPoint savedPivotScreenPos;
    bool doFlipbookAlign = false;
    if (m_settings.flipbookMode != FlipbookMode::None && sprite && m_session->selectedSprite
            && sprite != m_session->selectedSprite) {
        if (m_settings.flipbookMode == FlipbookMode::All) {
            doFlipbookAlign = true;
        } else { // SameGroup
            const QString prevLabel = TimelineBuilder::groupLabelFor(m_session->selectedSprite->name);
            const QString newLabel  = TimelineBuilder::groupLabelFor(sprite->name);
            doFlipbookAlign = !prevLabel.isEmpty() && prevLabel == newLabel;
        }
        if (doFlipbookAlign)
            savedPivotScreenPos = previewView->mapFromScene(
                QPointF(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY));
    }

    // Flipbook: capture the actual view zoom now, before applySpriteSelection may change it
    // via the spin box (which can be stale relative to the actual transform).
    const double flipbookZoom = doFlipbookAlign ? previewView->transform().m11() : 0.0;

    m_session->selectedSprite = sprite;
    if (sprite) {
        m_statusLabel->setText(tr("Selected: ") + sprite->name);
    }
    if (spriteEditorPanel->spriteDimsLabel()) {
        if (sprite) {
            const int w = sprite->rotated ? sprite->rect.height() : sprite->rect.width();
            const int h = sprite->rotated ? sprite->rect.width()  : sprite->rect.height();
            if (spriteEditorPanel->spriteNameFooterLabel()) {
                spriteEditorPanel->spriteNameFooterLabel()->setFullText(sprite->name);
                spriteEditorPanel->spriteNameFooterLabel()->setVisible(true);
            }
            spriteEditorPanel->spriteDimsLabel()->setText(QString("%1 \xc3\x97 %2 px").arg(w).arg(h));
            spriteEditorPanel->spriteDimsLabel()->setVisible(true);
        } else {
            if (spriteEditorPanel->spriteNameFooterLabel())
                spriteEditorPanel->spriteNameFooterLabel()->setVisible(false);
            spriteEditorPanel->spriteDimsLabel()->setVisible(false);
        }
    }
    const FrameZoomMode effectiveZoomMode =
        (doFlipbookAlign || m_isRestoringProject) ? FrameZoomMode::Keep
                                                  : m_settings.frameZoomMode;
    SpriteSelectionPresenter::applySpriteSelection(
        sprite,
        m_session->selectedPointName,
        {spriteEditorPanel->spriteNameEdit(), spriteEditorPanel->pivotXSpin(), spriteEditorPanel->pivotYSpin(),
         spriteEditorPanel->configPointsBtn(), previewView, spriteEditorPanel->previewZoomSpin(), spriteEditorPanel->handleCombo()},
        effectiveZoomMode);

    if (doFlipbookAlign && sprite) {
        // Mark zoom as manual so any pending resize-debounce timer does not fire
        // initialFit() and overwrite the zoom we are about to restore.
        previewView->setZoomManual(true);
        previewView->setZoom(flipbookZoom);
        previewView->alignPivotToScreenPos(QPoint(sprite->pivotX, sprite->pivotY), savedPivotScreenPos);
    }

    m_atlasWorkspace->refreshSpriteEditor();

    if (spriteEditorPanel->editAliasesBtn()) spriteEditorPanel->editAliasesBtn()->setEnabled(sprite != nullptr);
    spriteEditorPanel->markerTemplatesBtn()->setEnabled(sprite != nullptr);
    updateAliasesButton();
    updateOnionSkinDisplay();
}

void MainWindow::updateAliasesButton() {
    auto* editAliasesBtn = m_atlasWorkspace->spriteEditorPanel()->editAliasesBtn();
    if (!editAliasesBtn) return;
    if (!m_session || !m_session->selectedSprite) {
        editAliasesBtn->setToolTip(tr("Edit sprite name aliases"));
        return;
    }
    const int count = m_session->selectedSprite->aliases.size();
    if (count > 0)
        editAliasesBtn->setToolTip(tr("Edit sprite name aliases (%1)").arg(count));
    else
        editAliasesBtn->setToolTip(tr("Edit sprite name aliases"));
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
    auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
    const QString requestedProfile = profileCombo ? profileCombo->currentData().toString() : QString();
    if (requestedProfile == m_currentProfile) return;

    if (m_layoutOrchestrator) m_layoutOrchestrator->clearProfilesTried();

    QString oldProfile = m_currentProfile;
    m_currentProfile = requestedProfile;

    m_undoStack->push(new SetProfileCommand(
        profileCombo,
        oldProfile,
        requestedProfile,
        [this]() {
            auto* combo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
            m_currentProfile = combo ? combo->currentData().toString() : QString();
            if (m_layoutOrchestrator) m_layoutOrchestrator->setCurrentProfile(m_currentProfile);
            scheduleLayoutRebuild(true);
        }
    ));

    if (m_layoutOrchestrator) m_layoutOrchestrator->setCurrentProfile(m_currentProfile);

    // Profile changes should rebuild layout immediately - user expects visual feedback
    scheduleLayoutRebuild(true);
}

void MainWindow::onLayoutZoomChanged(double value) {
    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
    if (canvas) {
        canvas->setZoomManual(true);
        canvas->setZoom(value / 100.0);
    }
}

