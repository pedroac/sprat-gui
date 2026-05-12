#include "MainWindow.h"
#include "AnimationCanvas.h"
#include "ImportPathSupport.h"

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
#include <QIcon>
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
#ifdef Q_OS_WASM
    Q_UNUSED(event);
    return;
#endif
    if (!event->mimeData()->hasUrls()) {
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() != 1) {
        return;
    }
    const QUrl url = urls.first();
    if (url.isLocalFile()) {
        const QString localPath = url.toLocalFile();
        if (isSupportedDropPath(localPath)) {
            event->acceptProposedAction();
        }
    } else if (tryHandleRemoteUrl(url, DropAction::Cancel)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
#ifdef Q_OS_WASM
    Q_UNUSED(event);
    return;
#endif
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() != 1) {
        return;
    }
    const QUrl url = urls.first();
    if (url.isLocalFile()) {
        const QString localPath = url.toLocalFile();
        if (localPath.isEmpty()) {
            return;
        }
        DropAction action = confirmDropAction(localPath);
        if (action != DropAction::Cancel && tryHandleDroppedPath(localPath, action)) {
            event->acceptProposedAction();
        }
    } else {
        DropAction action = confirmDropAction(url.toString());
        if (action != DropAction::Cancel && tryHandleRemoteUrl(url, action)) {
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
    return ImportPathSupport::isSupportedLocalImportPath(info.filePath());
}

MainWindow::DropAction MainWindow::confirmDropAction(const QString& path) {
    if (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty()) {
        return DropAction::Replace;
    }

    QMessageBox msg(this);
    msg.setWindowTitle(tr("Layout Already Loaded"));
    msg.setText(QString(tr("A layout is already loaded. What would you like to do with '%1'?")).arg(QFileInfo(path).fileName()));

    QPushButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
    replaceBtn->setIcon(QIcon::fromTheme("document-save"));
    QPushButton* mergeBtn = msg.addButton(tr("Merge"), QMessageBox::AcceptRole);
    mergeBtn->setIcon(QIcon::fromTheme("edit-paste"));
    QPushButton* cancelBtn = msg.addButton(tr("Cancel"), QMessageBox::RejectRole);
    cancelBtn->setIcon(QIcon::fromTheme("process-stop"));
    
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
    
    const QString lowerPath = info.filePath().toLower();
    if (lowerPath.endsWith(".tar") || lowerPath.endsWith(".tar.gz") ||
        lowerPath.endsWith(".tar.bz2") || lowerPath.endsWith(".tar.xz")) {
        loadTarFile(path, action);
        return true;
    }

    if (lowerPath.endsWith(".png") || lowerPath.endsWith(".jpg") ||
        lowerPath.endsWith(".jpeg") || lowerPath.endsWith(".bmp") ||
        lowerPath.endsWith(".gif") || lowerPath.endsWith(".webp") ||
        lowerPath.endsWith(".tga") || lowerPath.endsWith(".dds")) {
        loadImageWithFrameDetection(path, action);
        return true;
    }
    
    loadProject(path, action);
    return true;
}

void MainWindow::onLayoutCanvasPathDropped(const QString& path) {
    DropAction action = confirmDropAction(path);
    if (action != DropAction::Cancel) {
        const QUrl url(path);
        if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile() && tryHandleRemoteUrl(url, action)) {
            return;
        }
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
        QTimer::singleShot(0, this, &MainWindow::fitAnimationToViewport);
    }
    QTimer::singleShot(0, this, &MainWindow::refreshAnimationTest);
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
