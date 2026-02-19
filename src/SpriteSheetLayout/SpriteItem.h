#pragma once
#include <QGraphicsPixmapItem>
#include <QPen>
#include "models.h"

class SpriteItem : public QGraphicsPixmapItem {
public:
    SpriteItem(SpritePtr data, QGraphicsItem* parent = nullptr);
    SpritePtr getData() const { return m_data; }
    
    bool isSelectedState() const { return m_isSelected; }
    void setSelectedState(bool selected);
    void setSearchMatch(bool match);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    SpritePtr m_data;
    bool m_isSelected = false;
    bool m_isMatch = false;
};