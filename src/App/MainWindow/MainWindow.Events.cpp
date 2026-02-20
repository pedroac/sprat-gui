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
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>

namespace {
constexpr double kAnimPreviewZoomScaleFactor = 1.15;
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
    if ((watched == m_animPreviewLabel ||
         (m_animPreviewScroll && watched == m_animPreviewScroll->viewport())) &&
        handleAnimPreviewEvent(event)) {
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
            return handleAnimPreviewMousePress(static_cast<QMouseEvent*>(event));
        case QEvent::MouseMove:
            return handleAnimPreviewMouseMove(static_cast<QMouseEvent*>(event));
        case QEvent::MouseButtonRelease:
            return handleAnimPreviewMouseRelease(static_cast<QMouseEvent*>(event));
        case QEvent::KeyPress:
            return handleAnimPreviewKeyPress(static_cast<QKeyEvent*>(event));
        case QEvent::KeyRelease:
            return handleAnimPreviewKeyRelease(static_cast<QKeyEvent*>(event));
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

bool MainWindow::handleAnimPreviewMousePress(QMouseEvent* mouseEvent) {
    if (m_animPreviewScroll && m_animPreviewScroll->viewport()) {
        m_animPreviewScroll->viewport()->setFocus();
        if (mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::LeftButton && m_animPreviewSpacePressed)) {
            m_animPreviewPanning = true;
            m_animPreviewLastMousePos = mouseEvent->pos();
            m_animPreviewScroll->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        return false;
    }
    m_animPreviewLabel->setFocus();
    return false;
}

bool MainWindow::handleAnimPreviewMouseMove(QMouseEvent* mouseEvent) {
    if (!m_animPreviewPanning || !m_animPreviewScroll) {
        return false;
    }
    const QPoint delta = mouseEvent->pos() - m_animPreviewLastMousePos;
    m_animPreviewLastMousePos = mouseEvent->pos();
    m_animPreviewScroll->horizontalScrollBar()->setValue(m_animPreviewScroll->horizontalScrollBar()->value() - delta.x());
    m_animPreviewScroll->verticalScrollBar()->setValue(m_animPreviewScroll->verticalScrollBar()->value() - delta.y());
    return true;
}

bool MainWindow::handleAnimPreviewMouseRelease(QMouseEvent* mouseEvent) {
    if (!m_animPreviewPanning) {
        return false;
    }
    if (mouseEvent->button() != Qt::MiddleButton && mouseEvent->button() != Qt::LeftButton) {
        return false;
    }
    m_animPreviewPanning = false;
    if (m_animPreviewScroll && m_animPreviewScroll->viewport()) {
        m_animPreviewScroll->viewport()->setCursor(m_animPreviewSpacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    return true;
}

bool MainWindow::handleAnimPreviewKeyPress(QKeyEvent* keyEvent) {
    if (keyEvent->key() != Qt::Key_Space || keyEvent->isAutoRepeat()) {
        return false;
    }
    m_animPreviewSpacePressed = true;
    if (!m_animPreviewPanning && m_animPreviewScroll && m_animPreviewScroll->viewport()) {
        m_animPreviewScroll->viewport()->setCursor(Qt::OpenHandCursor);
    }
    return false;
}

bool MainWindow::handleAnimPreviewKeyRelease(QKeyEvent* keyEvent) {
    if (keyEvent->key() != Qt::Key_Space || keyEvent->isAutoRepeat()) {
        return false;
    }
    m_animPreviewSpacePressed = false;
    if (!m_animPreviewPanning && m_animPreviewScroll && m_animPreviewScroll->viewport()) {
        m_animPreviewScroll->viewport()->setCursor(Qt::ArrowCursor);
    }
    return false;
}

bool MainWindow::handleAnimPreviewWheel(QWheelEvent* wheelEvent) {
    if (!(wheelEvent->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    double zoom = m_animZoomSpin->value();
    zoom = wheelEvent->angleDelta().y() > 0 ? zoom * kAnimPreviewZoomScaleFactor : zoom / kAnimPreviewZoomScaleFactor;
    m_animZoomSpin->setValue(zoom);
    return true;
}

void MainWindow::handleAnimPreviewResize() {
    refreshAnimationTest();
}

bool MainWindow::handleAnimPreviewContextMenu(QContextMenuEvent* contextEvent) {
    if (m_animPreviewPanning) {
        return true;
    }
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
    } else if (selectedAction == copyFrame && !m_animPreviewLabel->pixmap().isNull()) {
        QApplication::clipboard()->setPixmap(m_animPreviewLabel->pixmap());
    }
    return true;
}
