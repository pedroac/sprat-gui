#include "PackedAtlasView.h"
#include "ArchiveExtractor.h"
#include "ViewUtils.h"

#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QTemporaryDir>

static void applyBackground(QGraphicsView* view, const AppSettings& settings) {
    view->setBackgroundBrush(settings.workspaceColor);
}

static QBrush makeAtlasBrush(const AppSettings& settings) {
    if (settings.showCheckerboard) {
        return QBrush(createCheckerboardPixmap(settings.spriteFrameColor));
    } else {
        return QBrush(settings.spriteFrameColor);
    }
}

PackedAtlasView::PackedAtlasView(QWidget* parent)
    : ZoomableGraphicsView(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    applyBackground(this, m_settings);

    m_bgRectItem = new QGraphicsRectItem();
    m_bgRectItem->setPen(Qt::NoPen);
    m_bgRectItem->setBrush(makeAtlasBrush(m_settings));
    m_bgRectItem->setZValue(-1);
    m_bgRectItem->hide();
    m_scene->addItem(m_bgRectItem);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::FastTransformation);
    m_scene->addItem(m_pixmapItem);

    // Full-view overlay for Loading / Error / Idle states
    m_overlayLabel = new QLabel(this);
    m_overlayLabel->setAlignment(Qt::AlignCenter);
    m_overlayLabel->setWordWrap(true);
    m_overlayLabel->setStyleSheet(
        "background: rgba(0,0,0,140); color: white; font-size: 14px; padding: 12px;");

    // Small top banner for multipack info
    m_bannerLabel = new QLabel(this);
    m_bannerLabel->setAlignment(Qt::AlignCenter);
    m_bannerLabel->setStyleSheet(
        "background: rgba(30,60,160,210); color: white; font-size: 12px; padding: 3px 8px;");
    m_bannerLabel->hide();

    setIdle();
}

void PackedAtlasView::setSettings(const AppSettings& settings) {
    m_settings = settings;
    applyBackground(this, m_settings);
    if (m_bgRectItem) m_bgRectItem->setBrush(makeAtlasBrush(m_settings));
}

void PackedAtlasView::resizeEvent(QResizeEvent* event) {
    ZoomableGraphicsView::resizeEvent(event);
    updateOverlayGeometry();
}

void PackedAtlasView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    ZoomableGraphicsView::mousePressEvent(event);
}

void PackedAtlasView::updateOverlayGeometry() {
    if (m_overlayLabel) m_overlayLabel->setGeometry(rect());
    if (m_bannerLabel) {
        const int h = m_bannerLabel->sizeHint().height() + 6;
        m_bannerLabel->setGeometry(0, 0, width(), h);
    }
}

void PackedAtlasView::setIdle() {
    m_state = State::Idle;
    m_bannerLabel->hide();
    m_bgRectItem->hide();
    m_pixmapItem->setPixmap(QPixmap());
    m_overlayLabel->setText(tr("No preview available"));
    m_overlayLabel->show();
    m_overlayLabel->raise();
}

void PackedAtlasView::setLoading() {
    m_state = State::Loading;
    m_bannerLabel->hide();
    m_bgRectItem->hide();
    m_pixmapItem->setPixmap(QPixmap());
    m_overlayLabel->setText(tr("Loading atlas image\u2026"));
    m_overlayLabel->show();
    m_overlayLabel->raise();
}

void PackedAtlasView::setError(const QString& message) {
    m_state = State::Error;
    m_bannerLabel->hide();
    m_bgRectItem->hide();
    m_pixmapItem->setPixmap(QPixmap());
    m_overlayLabel->setText(tr("Preview error:\n%1").arg(message));
    m_overlayLabel->show();
    m_overlayLabel->raise();
}

void PackedAtlasView::setImage(const QByteArray& pngData) {
    static const QByteArray kPngMagic("\x89PNG\r\n\x1a\n", 8);

    if (pngData.isEmpty()) {
        setError(tr("Received empty image data"));
        return;
    }

    if (!pngData.startsWith(kPngMagic)) {
        // Multipack result — tar archive
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            setError(tr("Could not create temporary directory for multipack"));
            return;
        }

        const QString tarPath = QDir(tempDir.path()).filePath("multipack.tar");
        QFile tarFile(tarPath);
        if (!tarFile.open(QIODevice::WriteOnly)) {
            setError(tr("Could not write multipack archive"));
            return;
        }
        tarFile.write(pngData);
        tarFile.close();

        QString extractError;
        if (!ArchiveExtractor::extractToDirectory(tarPath, tempDir.path(), extractError)) {
            setError(tr("Could not extract multipack: %1").arg(extractError));
            return;
        }

        const QDir dir(tempDir.path());
        const QStringList pngFiles = dir.entryList({"*.png"}, QDir::Files, QDir::Name);
        if (pngFiles.isEmpty()) {
            setError(tr("No PNG files found in multipack archive"));
            return;
        }

        QPixmap pixmap;
        if (!pixmap.load(dir.filePath(pngFiles.first()))) {
            setError(tr("Could not load first sheet from multipack"));
            return;
        }

        showPixmap(pixmap, tr("Multipack — showing sheet 1 of %1").arg(pngFiles.size()));
        return;
    }

    QPixmap pixmap;
    if (!pixmap.loadFromData(pngData, "PNG")) {
        setError(tr("Could not decode PNG image"));
        return;
    }
    showPixmap(pixmap, QString());
}

void PackedAtlasView::showPixmap(const QPixmap& pixmap, const QString& bannerText) {
    const QRectF imageRect(QPointF(0, 0), pixmap.size());
    m_bgRectItem->setRect(imageRect);
    m_bgRectItem->show();
    m_pixmapItem->setPixmap(pixmap);
    m_scene->setSceneRect(imageRect);
    m_overlayLabel->hide();
    m_state = State::Ready;

    if (!bannerText.isEmpty()) {
        m_bannerLabel->setText(bannerText);
        m_bannerLabel->show();
        m_bannerLabel->raise();
        updateOverlayGeometry();
    } else {
        m_bannerLabel->hide();
    }

    if (!m_isZoomManual) {
        initialFit();
    }
}
