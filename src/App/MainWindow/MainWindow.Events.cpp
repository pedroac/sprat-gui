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
    const QString localPath = urls.first().toLocalFile();
    if (isSupportedDropPath(localPath)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() != 1 || !urls.first().isLocalFile()) {
        appendDebugLog("Drop ignored on release: expected exactly one local file/folder.");
        return;
    }
    const QString localPath = urls.first().toLocalFile();
    appendDebugLog(QString("Drop received: '%1'").arg(localPath));
    if (tryHandleDroppedPath(localPath, true)) {
        event->acceptProposedAction();
        appendDebugLog("Drop handled successfully.");
    } else {
        appendDebugLog(QString("Drop was not handled: '%1'").arg(localPath));
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
    appendDebugLog(QString("Handling dropped path: '%1'").arg(path));
    if (!isSupportedDropPath(path)) {
        appendDebugLog(QString("Dropped path is unsupported: '%1'").arg(path));
        return false;
    }
    QFileInfo info(path);
    QPixmapCache::clear();
    if (info.isDir()) {
        QDir dir(path);
        if (dir.exists("project.spart.json")) {
            appendDebugLog(QString("Detected project folder. Loading '%1'.").arg(dir.filePath("project.spart.json")));
            loadProject(dir.filePath("project.spart.json"), confirmReplace);
        } else {
            appendDebugLog(QString("Detected frames folder. Loading '%1'.").arg(path));
            loadFolder(path, confirmReplace);
        }
        return true;
    }
    appendDebugLog(QString("Detected project file. Loading '%1'.").arg(path));
    loadProject(path, confirmReplace);
    return true;
}

void MainWindow::onLayoutCanvasPathDropped(const QString& path) {
    appendDebugLog(QString("Canvas external drop: '%1'").arg(path));
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
