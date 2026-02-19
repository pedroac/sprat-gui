#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPixmapCache>
#include <QStandardPaths>
#include <QWheelEvent>

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() != 1 || !urls.first().isLocalFile()) {
        return;
    }
    if (isSupportedDropPath(urls.first().toLocalFile())) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() != 1 || !urls.first().isLocalFile()) {
        return;
    }
    if (tryHandleDroppedPath(urls.first().toLocalFile(), true)) {
        event->acceptProposedAction();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_animPreviewLabel && handleAnimPreviewEvent(event)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::isSupportedDropPath(const QString& path) const {
    QFileInfo info(path);
    if (info.isDir()) {
        return true;
    }
    const QString ext = info.suffix().toLower();
    return ext == "zip" || ext == "json";
}

bool MainWindow::tryHandleDroppedPath(const QString& path, bool confirmReplace) {
    if (!isSupportedDropPath(path)) {
        return false;
    }
    QFileInfo info(path);
    QPixmapCache::clear();
    if (info.isDir()) {
        QDir dir(path);
        if (dir.exists("project.spart.json")) {
            loadProject(dir.filePath("project.spart.json"), confirmReplace);
        } else {
            loadFolder(path, confirmReplace);
        }
        return true;
    }
    loadProject(path, confirmReplace);
    return true;
}

void MainWindow::onLayoutCanvasPathDropped(const QString& path) {
    tryHandleDroppedPath(path, true);
}

bool MainWindow::handleAnimPreviewEvent(QEvent* event) {
    switch (event->type()) {
        case QEvent::MouseButtonPress:
            handleAnimPreviewMousePress();
            return false;
        case QEvent::Wheel:
            return handleAnimPreviewWheel(static_cast<QWheelEvent*>(event));
        case QEvent::Resize:
            handleAnimPreviewResize();
            return false;
        case QEvent::ContextMenu:
            return handleAnimPreviewContextMenu(static_cast<QContextMenuEvent*>(event));
        default:
            return false;
    }
}

void MainWindow::handleAnimPreviewMousePress() {
    m_animPreviewLabel->setFocus();
}

bool MainWindow::handleAnimPreviewWheel(QWheelEvent* wheelEvent) {
    if (!(wheelEvent->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const double scaleFactor = 1.15;
    double zoom = m_animZoomSpin->value();
    zoom = wheelEvent->angleDelta().y() > 0 ? zoom * scaleFactor : zoom / scaleFactor;
    m_animZoomSpin->setValue(zoom);
    return true;
}

void MainWindow::handleAnimPreviewResize() {
    refreshAnimationTest();
}

bool MainWindow::handleAnimPreviewContextMenu(QContextMenuEvent* contextEvent) {
    QMenu menu(this);
    QString ffmpegExe = QStandardPaths::findExecutable("ffmpeg");
    QString magickExe = QStandardPaths::findExecutable("magick");
    if (magickExe.isEmpty()) {
        magickExe = QStandardPaths::findExecutable("convert");
    }
    const bool hasExportTools = !ffmpegExe.isEmpty() || !magickExe.isEmpty();

    QAction* saveAnim = menu.addAction("Save Animation...");
    saveAnim->setEnabled(hasExportTools);
    QAction* copyFrame = menu.addAction("Copy Current Frame");

    QAction* selectedAction = menu.exec(contextEvent->globalPos());
    if (selectedAction == saveAnim) {
        saveAnimationToFile();
    } else if (selectedAction == copyFrame && !m_animPreviewLabel->pixmap().isNull()) {
        QApplication::clipboard()->setPixmap(m_animPreviewLabel->pixmap());
    }
    return true;
}
