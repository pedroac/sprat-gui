#include "AnimationCanvas.h"
#include <QScrollBar>
#include <QApplication>

AnimationCanvas::AnimationCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(QColor(90, 90, 90));

    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
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
