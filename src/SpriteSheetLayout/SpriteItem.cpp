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
    if (!m_isSelected) m_isPrimary = false;
    update();
}

void SpriteItem::setPrimaryState(bool primary) {
    if (m_isPrimary == primary) return;
    m_isPrimary = primary;
    if (m_isPrimary) m_isSelected = true;
    update();
}

void SpriteItem::setContextTargetState(bool contextTarget) {
    if (m_isContextTarget == contextTarget) return;
    m_isContextTarget = contextTarget;
    update();
}

void SpriteItem::setSearchMatch(bool match) {
    if (m_isMatch == match) return;
    m_isMatch = match;
    update();
}

void SpriteItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    // Pre-allocated drawing resources
    static const QColor kChipBgColor(0, 0, 0, 160);
    static const QColor kContextTargetColor(255, 215, 0, 64);
    static const QColor kPrimaryColor(10, 125, 255, 64);
    static const QColor kSecondaryColor(44, 201, 97);
    static const QColor kSecondaryFillColor(44, 201, 97, 56);
    static const QPen kSecondaryPen = []() {
        QPen p(kSecondaryColor, 2);
        p.setCosmetic(true);
        return p;
    }();
    static const QFont kChipFont = []() {
        QFont f;
        f.setPixelSize(11);
        f.setBold(false);
        return f;
    }();
    static const QFontMetrics kChipFm(kChipFont);

    QGraphicsPixmapItem::paint(painter, option, widget);

    // Draw Name Chip (hidden during animations or when label mode is None)
    if (!m_labelHidden && m_labelMode != LayoutLabelMode::None) {
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

            painter->setFont(kChipFont);

            int padding = 4;
            int maxWidth = static_cast<int>(screenWidth) - (padding * 2);

            if (maxWidth > 20) {
                const QString labelText = (m_labelMode == LayoutLabelMode::FullPath)
                    ? m_data->path : m_data->name;
                QString elidedText = kChipFm.elidedText(labelText, Qt::ElideRight, maxWidth);
                int textWidth = kChipFm.horizontalAdvance(elidedText);
                int textHeight = kChipFm.height();

                // Chip rectangle (centered horizontally, just above bottom)
                QRectF chipRect(-textWidth / 2.0 - padding, -textHeight - padding - 2, textWidth + padding * 2, textHeight + padding);

                painter->setPen(Qt::NoPen);
                painter->setBrush(kChipBgColor);
                painter->drawRoundedRect(chipRect, 4, 4);

                painter->setPen(Qt::white);
                painter->drawText(chipRect, Qt::AlignCenter, elidedText);
            }

            painter->restore();
        }
    }

    if (m_isContextTarget) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(kContextTargetColor);
        painter->drawRect(boundingRect());
    }

    if (m_isPrimary) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(kPrimaryColor);
        painter->drawRect(boundingRect());
    } else if (m_isSelected || m_isMatch) {
        painter->setPen(kSecondaryPen);
        painter->setBrush(kSecondaryFillColor);
        painter->drawRect(boundingRect());
    }
}
