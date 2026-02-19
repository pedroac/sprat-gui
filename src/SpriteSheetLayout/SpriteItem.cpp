#include "SpriteItem.h"
#include <QPainter>
#include <QFontMetrics>
#include <QStyleOptionGraphicsItem>

SpriteItem::SpriteItem(SpritePtr data, QGraphicsItem* parent)
    : QGraphicsPixmapItem(parent), m_data(data)
{
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setAcceptHoverEvents(true);
    
    // Tooltip
    setToolTip(m_data->name);
}

void SpriteItem::setSelectedState(bool selected) {
    if (m_isSelected == selected) return;
    m_isSelected = selected;
    update();
}

void SpriteItem::setSearchMatch(bool match) {
    if (m_isMatch == match) return;
    m_isMatch = match;
    update();
}

void SpriteItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsPixmapItem::paint(painter, option, widget);

    // Draw Name Chip
    qreal lod = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->transform());
    QRectF rect = boundingRect();
    double screenWidth = rect.width() * lod;

    if (screenWidth >= 40) {
        painter->save();
        
        // Draw at constant screen size
        double scale = 1.0 / lod;
        QPointF centerBottom(rect.center().x(), rect.bottom());
        painter->translate(centerBottom);
        painter->scale(scale, scale);
        
        QFont font = painter->font();
        font.setPixelSize(11);
        font.setBold(false);
        painter->setFont(font);
        QFontMetrics fm(font);
        
        int padding = 4;
        int maxWidth = static_cast<int>(screenWidth) - (padding * 2);
        
        if (maxWidth > 20) {
            QString elidedText = fm.elidedText(m_data->name, Qt::ElideRight, maxWidth);
            int textWidth = fm.horizontalAdvance(elidedText);
            int textHeight = fm.height();
            
            // Chip rectangle (centered horizontally, just above bottom)
            QRectF chipRect(-textWidth / 2.0 - padding, -textHeight - padding - 2, textWidth + padding * 2, textHeight + padding);
            
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 160));
            painter->drawRoundedRect(chipRect, 4, 4);
            
            painter->setPen(Qt::white);
            painter->drawText(chipRect, Qt::AlignCenter, elidedText);
        }
        
        painter->restore();
    }

    if (m_isSelected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(10, 125, 255, 64));
        painter->drawRect(boundingRect());
    }

    if (m_isMatch) {
        // Search match highlight (green)
        QPen pen(QColor(44, 201, 97), 2);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(QColor(44, 201, 97, 56));
        painter->drawRect(boundingRect());
    }
}