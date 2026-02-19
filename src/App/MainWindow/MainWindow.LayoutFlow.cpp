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

namespace {
LayoutModel cloneLayoutModel(const LayoutModel& src) {
    LayoutModel out;
    out.atlasWidth = src.atlasWidth;
    out.atlasHeight = src.atlasHeight;
    out.scale = src.scale;
    out.sprites.reserve(src.sprites.size());
    for (const auto& sprite : src.sprites) {
        auto copy = std::make_shared<Sprite>();
        copy->path = sprite->path;
        copy->name = sprite->name;
        copy->rect = sprite->rect;
        copy->trimmed = sprite->trimmed;
        copy->trimRect = sprite->trimRect;
        copy->pivotX = sprite->pivotX;
        copy->pivotY = sprite->pivotY;
        copy->points = sprite->points;
        out.sprites.append(copy);
    }
    return out;
}

}

void MainWindow::onRunLayout() {
    if (m_currentFolder.isEmpty()) {
        return;
    }
    if (!m_cliReady) {
        checkCliTools();
        return;
    }

    if (!m_activeLayoutCacheKey.isEmpty() && !m_layoutModel.sprites.isEmpty()) {
        LayoutCacheEntry entry;
        entry.folder = m_currentFolder;
        entry.padding = m_paddingSpin->value();
        entry.trimTransparent = m_trimCheck->isChecked();
        entry.model = cloneLayoutModel(m_layoutModel);
        m_layoutCache[m_activeLayoutCacheKey] = entry;
    }
    const QString requestedProfile = m_profileCombo->currentText();
    if (m_layoutCache.contains(requestedProfile)) {
        const LayoutCacheEntry& cached = m_layoutCache[requestedProfile];
        if (cached.folder != m_currentFolder ||
            cached.padding != m_paddingSpin->value() ||
            cached.trimTransparent != m_trimCheck->isChecked()) {
            // Same profile key, but options changed: treat as miss and replace after fresh load.
        } else {
        QStringList selectedPaths;
        for (const auto& s : m_selectedSprites) {
            selectedPaths << s->path;
        }
        const QString primaryPath = m_selectedSprite ? m_selectedSprite->path : QString();

        m_layoutModel = cloneLayoutModel(cached.model);
        m_canvas->setModel(m_layoutModel);
        m_statusLabel->setText(QString("Loaded %1 sprites (cached)").arg(m_layoutModel.sprites.size()));
        if (!m_pendingProjectPayload.isEmpty()) {
            applyProjectPayload();
        } else if (!selectedPaths.isEmpty()) {
            m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
        }
        m_activeLayoutCacheKey = requestedProfile;
        updateMainContentView();
        updateUiState();
        if (m_isLoading) {
            setLoading(false);
        }
        return;
        }
    }

    QStringList args;
    args << m_currentFolder;
    args << "--profile" << m_profileCombo->currentText();
    args << "--padding" << QString::number(m_paddingSpin->value());
    if (m_trimCheck->isChecked()) {
        args << "--trim-transparent";
    }

    m_statusLabel->setText("Running spratlayout...");
    setLoading(true);
    m_process->start(m_spratLayoutBin, args);
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    setLoading(false);
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        const QString err = m_process->readAllStandardError();
        m_statusLabel->setText("Error running layout");
        qCritical() << "spratlayout process failed. Exit code:" << exitCode << "Error:" << err;
        QMessageBox::critical(this, "Error", "spratlayout failed:\n" + err);
        return;
    }

    LayoutModel newModel = parseLayoutOutput(m_process->readAllStandardOutput(), m_currentFolder);
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

    if (!m_pendingProjectPayload.isEmpty()) {
        applyProjectPayload();
    } else if (!selectedPaths.isEmpty()) {
        m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
    }

    LayoutCacheEntry entry;
    entry.folder = m_currentFolder;
    entry.padding = m_paddingSpin->value();
    entry.trimTransparent = m_trimCheck->isChecked();
    entry.model = cloneLayoutModel(m_layoutModel);
    m_activeLayoutCacheKey = m_profileCombo->currentText();
    m_layoutCache[m_activeLayoutCacheKey] = entry;

    updateMainContentView();
    updateUiState();
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    setLoading(false);
    if (error == QProcess::FailedToStart) {
        m_statusLabel->setText("Error: spratlayout not found");
        qCritical() << "Failed to start spratlayout process. Make sure it is installed and in your PATH.";
        QMessageBox::critical(this, "Error", "Could not start 'spratlayout'.\nMake sure it is installed and in your PATH.");
        return;
    }
    m_statusLabel->setText("Error running layout process");
}

void MainWindow::setLoading(bool loading) {
    m_isLoading = loading;
    if (loading) {
        if (m_welcomeLabel && m_layoutModel.sprites.isEmpty()) {
            m_welcomeLabel->setText(m_loadingUiMessage);
        }
        if (m_mainStack && m_welcomePage && m_layoutModel.sprites.isEmpty()) {
            m_mainStack->setCurrentWidget(m_welcomePage);
        }
        if (m_cliInstallOverlay && m_cliInstallOverlayLabel) {
            m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
            if (m_cliInstallProgress) {
                m_cliInstallProgress->hide();
            }
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_cliInstallOverlay->show();
            m_cliInstallOverlay->raise();
        }
    } else {
        if (m_welcomeLabel) {
            m_welcomeLabel->setText("Drag and Drop folder with image files");
        }
        if (m_cliInstallOverlay) {
            if (m_cliInstallProgress) {
                m_cliInstallProgress->show();
            }
            m_cliInstallOverlay->hide();
            m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
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
