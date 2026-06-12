#pragma once
#include <QWidget>
#include <QPixmap>

/**
 * @brief Floating image preview popup for sprite items in the navigator.
 *
 * Shows a checkerboard background and a scaled-down copy of the sprite image
 * near the cursor. Displayed with a short hover delay by NavigatorTreeWidget.
 */
class SpritePreviewTooltip : public QWidget {
    Q_OBJECT
public:
    explicit SpritePreviewTooltip(QWidget* parent = nullptr);

    /**
     * @brief Load the sprite at @p spritePath and show the popup near @p globalPos.
     */
    void showAt(const QString& spritePath, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMaxSize      = 192;
    static constexpr int kPadding      = 6;
    static constexpr int kCursorOffset = 16;

    QPixmap m_pixmap;
};
