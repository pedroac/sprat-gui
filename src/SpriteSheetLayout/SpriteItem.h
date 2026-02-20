#pragma once
#include <QGraphicsPixmapItem>
#include <QPen>
#include "models.h"

/**
 * @class SpriteItem
 * @brief Graphics item representing a sprite in the layout canvas.
 * 
 * This class extends QGraphicsPixmapItem to provide visual representation
 * of sprites in the layout canvas, including selection states, context
 * target highlighting, and search match indication.
 */
class SpriteItem : public QGraphicsPixmapItem {
public:
    /**
     * @brief Constructor for SpriteItem.
     * 
     * @param data Sprite data to associate with this item
     * @param parent Parent graphics item (optional)
     */
    SpriteItem(SpritePtr data, QGraphicsItem* parent = nullptr);

    /**
     * @brief Gets the sprite data associated with this item.
     * 
     * @return SpritePtr Shared pointer to the sprite data
     */
    SpritePtr getData() const { return m_data; }

    /**
     * @brief Checks if this item is in selected state.
     * 
     * @return bool True if selected, false otherwise
     */
    bool isSelectedState() const { return m_isSelected; }

    /**
     * @brief Sets the selected state of this item.
     * 
     * @param selected True to select, false to deselect
     */
    void setSelectedState(bool selected);

    /**
     * @brief Sets the context target state of this item.
     * 
     * Context target state is used to highlight the item when
     * it's the target of a context menu operation.
     * 
     * @param contextTarget True to set as context target, false otherwise
     */
    void setContextTargetState(bool contextTarget);

    /**
     * @brief Sets the search match state of this item.
     * 
     * Search match state is used to indicate when the item matches
     * a search query in the layout canvas.
     * 
     * @param match True if item matches search, false otherwise
     */
    void setSearchMatch(bool match);

protected:
    /**
     * @brief Custom paint implementation for the sprite item.
     * 
     * This method handles the visual representation of the sprite,
     * including selection highlights, context target indicators,
     * and search match overlays.
     * 
     * @param painter Painter to use for drawing
     * @param option Style options for the item
     * @param widget Widget that the item is being painted on
     */
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    SpritePtr m_data;                    ///< Sprite data associated with this item
    bool m_isSelected = false;           ///< Whether this item is selected
    bool m_isContextTarget = false;      ///< Whether this item is the context target
    bool m_isMatch = false;              ///< Whether this item matches the current search
};
