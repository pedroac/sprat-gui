#include "AnimationCanvas.h"
#include "AnimationPreviewService.h"
#include <QScrollBar>
#include <QApplication>
#include "ViewUtils.h"

AnimationCanvas::AnimationCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    if (m_settings.showCheckerboard) {
        setBackgroundBrush(QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
    } else {
        setBackgroundBrush(m_settings.spriteFrameColor);
    }

    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);

    m_overlay = new EditorOverlayItem();
    m_overlay->setVisible(false);
    m_scene->addItem(m_overlay);

}

void AnimationCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    if (m_settings.showCheckerboard) {
        setBackgroundBrush(QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
    } else {
        setBackgroundBrush(m_settings.spriteFrameColor);
    }
}

void AnimationCanvas::setPixmap(const QPixmap& pixmap) {
    m_pixmapItem->setPixmap(pixmap);
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
    if (m_overlay->isVisible() && m_overlaySprite) {
        m_overlay->setSceneSize(pixmap.size());
        // Reposition after every composite update so the overlay stays aligned
        // even when the bounds change (e.g. after a pivot drag is committed).
        m_overlay->setPos(
            AnimationPreviewService::cachedBoundsLeft() - m_overlaySprite->pivotX,
            AnimationPreviewService::cachedBoundsTop()  - m_overlaySprite->pivotY);
        m_overlay->updateLayout();
    }
}

void AnimationCanvas::centerContent() {
    if (m_pixmapItem) {
        centerOn(m_pixmapItem);
    }
}

void AnimationCanvas::setOverlaySprite(SpritePtr sprite) {
    m_overlaySprite = sprite;
    if (sprite) {
        m_overlay->setSprites({sprite});
        const QSize sz = m_pixmapItem->pixmap().size();
        if (!sz.isNull())
            m_overlay->setSceneSize(sz);
        // Position the overlay so that item-local coordinates match sprite-local
        // pixel coordinates.  The sprite is rendered at (maxLeft - pivotX, maxTop - pivotY)
        // in the composite, so placing the overlay there makes item-local (pivotX, pivotY)
        // map to the correct scene position (maxLeft, maxTop).
        m_overlay->setPos(
            AnimationPreviewService::cachedBoundsLeft() - sprite->pivotX,
            AnimationPreviewService::cachedBoundsTop()  - sprite->pivotY);
    } else {
        m_overlay->setSprites({});
        m_overlay->setPos(0, 0);
    }
    m_overlay->updateLayout();
}

void AnimationCanvas::setOverlayVisible(bool visible) {
    m_overlay->setVisible(visible);
    if (visible && m_overlaySprite) {
        const QSize sz = m_pixmapItem->pixmap().size();
        if (!sz.isNull())
            m_overlay->setSceneSize(sz);
        m_overlay->setPos(
            AnimationPreviewService::cachedBoundsLeft() - m_overlaySprite->pivotX,
            AnimationPreviewService::cachedBoundsTop()  - m_overlaySprite->pivotY);
        m_overlay->updateLayout();
    }
}

void AnimationCanvas::setOverlayEditable(bool editable) {
    m_overlay->setAcceptedMouseButtons(editable ? Qt::LeftButton : Qt::NoButton);
    m_overlay->setAcceptHoverEvents(editable);
}

