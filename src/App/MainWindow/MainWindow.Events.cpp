#include "MainWindow.h"
#include "FrameAnimationWorkspace.h"
#include "LayoutCanvas.h"
#include "LayoutOrchestrator.h"
#include "ImportPathSupport.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPixmapCache>

namespace {
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
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
    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
    if (canvas && watched == canvas->viewport() && m_layoutOrchestrator) {
        const auto type = event->type();
        if (type == QEvent::MouseButtonPress ||
            type == QEvent::MouseButtonRelease ||
            type == QEvent::Wheel) {
            m_layoutOrchestrator->resetDebounceTimer();
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

DropAction MainWindow::confirmDropAction(const QString& /*path*/) {
    // With the multi-source model the layout is always the union of all sources.
    // New files are always added as an additional source (Merge).
    // Replace is only used for the very first load when there is no content yet.
    const bool hasContent = m_session
        && !m_session->activeAtlas().layoutModels.isEmpty()
        && !m_session->activeAtlas().layoutModels.first().sprites.isEmpty();
    return hasContent ? DropAction::Merge : DropAction::Replace;
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

