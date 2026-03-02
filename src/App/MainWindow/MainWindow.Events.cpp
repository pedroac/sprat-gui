#include "MainWindow.h"
#include "AnimationCanvas.h"

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
#include <QMessageBox>
#include <QPushButton>
#include <QPixmapCache>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>

namespace {
}

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
        return;
    }
    const QString localPath = urls.first().toLocalFile();
    DropAction action = confirmDropAction(localPath);
    if (action != DropAction::Cancel) {
        if (tryHandleDroppedPath(localPath, action)) {
            event->acceptProposedAction();
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (m_animCanvas && (watched == m_animCanvas || watched == m_animCanvas->viewport())) {
        if (event->type() == QEvent::Resize) {
            handleAnimPreviewResize();
        } else if (event->type() == QEvent::ContextMenu) {
            return handleAnimPreviewContextMenu(static_cast<QContextMenuEvent*>(event));
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::isSupportedDropPath(const QString& path) const {
    QFileInfo info(path);
    if (info.isDir()) {
        return true;
    }
    const QString ext = info.suffix().toLower();
    return ext == "zip" || ext == "json" || ext == "tar" || ext == "tar.gz" || ext == "tar.bz2" || ext == "tar.xz" ||
           ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "webp" || ext == "tga" || ext == "dds";
}

MainWindow::DropAction MainWindow::confirmDropAction(const QString& path) {
    if (m_session->layoutModel.sprites.isEmpty()) {
        return DropAction::Replace;
    }

    QMessageBox msg(this);
    msg.setWindowTitle(tr("Layout Already Loaded"));
    msg.setText(QString(tr("A layout is already loaded. What would you like to do with '%1'?")).arg(QFileInfo(path).fileName()));
    
    QPushButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
    QPushButton* mergeBtn = msg.addButton(tr("Merge"), QMessageBox::AcceptRole);
    msg.addButton(tr("Cancel"), QMessageBox::RejectRole);
    
    msg.exec();
    
    if (msg.clickedButton() == replaceBtn) return DropAction::Replace;
    if (msg.clickedButton() == mergeBtn) return DropAction::Merge;
    return DropAction::Cancel;
}

bool MainWindow::tryHandleDroppedPath(const QString& path, DropAction action) {
    if (!isSupportedDropPath(path) || action == DropAction::Cancel) {
        return false;
    }
    QFileInfo info(path);
    QPixmapCache::clear();
    if (info.isDir()) {
        QDir dir(path);
        if (dir.exists("project.spart.json")) {
            loadProject(dir.filePath("project.spart.json"), action);
        } else {
            loadFolder(path, action);
        }
        return true;
    }
    
    const QString ext = info.suffix().toLower();
    if (ext == "tar" || ext == "tar.gz" || ext == "tar.bz2" || ext == "tar.xz") {
        loadTarFile(path, action);
        return true;
    }
    
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "webp" || ext == "tga" || ext == "dds") {
        loadImageWithFrameDetection(path, action);
        return true;
    }
    
    loadProject(path, action);
    return true;
}

void MainWindow::onLayoutCanvasPathDropped(const QString& path) {
    DropAction action = confirmDropAction(path);
    if (action != DropAction::Cancel) {
        tryHandleDroppedPath(path, action);
    }
}

bool MainWindow::handleAnimPreviewEvent(QEvent*) { return false; }
bool MainWindow::handleAnimPreviewMousePress(QMouseEvent*) { return false; }
bool MainWindow::handleAnimPreviewMouseMove(QMouseEvent*) { return false; }
bool MainWindow::handleAnimPreviewMouseRelease(QMouseEvent*) { return false; }
bool MainWindow::handleAnimPreviewKeyPress(QKeyEvent*) { return false; }
bool MainWindow::handleAnimPreviewKeyRelease(QKeyEvent*) { return false; }

bool MainWindow::handleAnimPreviewWheel(QWheelEvent*) {
    return false;
}

void MainWindow::handleAnimPreviewResize() {
    if (m_animCanvas && !m_animCanvas->isZoomManual()) {
        fitAnimationToViewport();
    }
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

    QAction* saveAnim = menu.addAction(tr("Save Animation..."));
    saveAnim->setEnabled(hasExportTools);
    QAction* copyFrame = menu.addAction(tr("Copy Current Frame"));

    QAction* selectedAction = menu.exec(contextEvent->globalPos());
    if (selectedAction == saveAnim) {
        saveAnimationToFile();
    } else if (selectedAction == copyFrame && m_animCanvas) {
        QApplication::clipboard()->setPixmap(m_animCanvas->grab());
    }
    return true;
}
