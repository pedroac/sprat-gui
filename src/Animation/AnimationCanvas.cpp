#include "AnimationCanvas.h"
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
    if (m_overlay->isVisible()) {
        m_overlay->setSceneSize(pixmap.size());
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
    } else {
        m_overlay->setSprites({});
    }
    m_overlay->updateLayout();
}

void AnimationCanvas::setOverlayVisible(bool visible) {
    m_overlay->setVisible(visible);
    if (visible && m_overlaySprite) {
        const QSize sz = m_pixmapItem->pixmap().size();
        if (!sz.isNull())
            m_overlay->setSceneSize(sz);
        m_overlay->updateLayout();
    }
}

void AnimationCanvas::setOverlayEditable(bool editable) {
    m_overlay->setAcceptedMouseButtons(editable ? Qt::LeftButton : Qt::NoButton);
    m_overlay->setAcceptHoverEvents(editable);
}

