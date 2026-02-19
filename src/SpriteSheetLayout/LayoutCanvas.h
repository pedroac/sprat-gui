#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QHash>
#include <QStringList>
#include "models.h"
#include "SpriteItem.h"

class LayoutCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);
    void setModel(const LayoutModel& model);
    void clearCanvas();
    void setZoom(double zoom);
    void selectSpriteByPath(const QString& path);
    void selectSpritesByPaths(const QStringList& paths, const QString& primaryPath = QString());
    void setSettings(const AppSettings& settings);

signals:
    void selectionChanged(const QList<SpritePtr>& selection);
    void zoomChanged(double zoom);
    void spriteSelected(SpritePtr sprite);
    void requestTimelineGeneration();
    void externalPathDropped(const QString& path);
    void addFramesRequested();
    void removeFramesRequested(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateSearch();
    void emitSelectionChanged();

    QGraphicsScene* m_scene;
    double m_zoomLevel = 1.0;
    bool m_isPanning = false;
    QPoint m_lastMousePos;
    bool m_spacePressed = false;
    QString m_searchQuery;
    QRect m_searchCloseRect;

    LayoutModel m_model;
    QVector<SpriteItem*> m_items;
    int m_lastSelectedIndex = -1;
    bool m_pendingDeselect = false;

    AppSettings m_settings;
    QGraphicsRectItem* m_atlasBgItem = nullptr;
    QList<QAbstractGraphicsShapeItem*> m_borderItems;
    QHash<QString, QPixmap> m_sourcePixmaps;
};
