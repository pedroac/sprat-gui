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
}

void AnimationCanvas::centerContent() {
    if (m_pixmapItem) {
        centerOn(m_pixmapItem);
    }
}
