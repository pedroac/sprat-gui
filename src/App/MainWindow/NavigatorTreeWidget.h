#pragma once
#include <QTreeWidget>
#include <QTimer>

class SpritePreviewTooltip;

class NavigatorTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit NavigatorTreeWidget(QWidget* parent = nullptr);

    /** Enable or disable the hover sprite preview tooltip. */
    void setPreviewEnabled(bool enabled);

    /** Set the delay in milliseconds before the preview appears after hovering. */
    void setPreviewDelay(int ms);

signals:
    void excludeRequested(QTreeWidgetItem* item);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void showPreview();

private:
    QTreeWidgetItem* m_checkboxAnchor = nullptr;

    // Hover preview state
    bool                  m_previewEnabled  = true;
    SpritePreviewTooltip* m_previewTooltip  = nullptr;
    QTreeWidgetItem*      m_hoveredItem     = nullptr;
    QPoint                m_hoverGlobalPos;
    QTimer                m_hoverTimer;

    // Sets the check state for all visible items between 'from' and 'to' (inclusive, visual order).
    void setCheckStateRange(QTreeWidgetItem* from, QTreeWidgetItem* to, Qt::CheckState state);
};
