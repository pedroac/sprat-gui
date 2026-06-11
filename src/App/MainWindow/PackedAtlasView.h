#pragma once
#include "ZoomableGraphicsView.h"
#include "IAtlasViewport.h"
#include "models.h"

class QLabel;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsRectItem;

class PackedAtlasView : public ZoomableGraphicsView, public IAtlasViewport {
    Q_OBJECT
public:
    enum class State { Idle, Loading, Ready, Error };

    explicit PackedAtlasView(QWidget* parent = nullptr);

    void setSettings(const AppSettings& settings);
    void setLoading();
    void setImage(const QByteArray& pngData);
    void setError(const QString& message);
    void setIdle();

    QWidget* widget() override { return this; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void showPixmap(const QPixmap& pixmap, const QString& bannerText = QString());
    void updateOverlayGeometry();

    AppSettings          m_settings;
    State                m_state        = State::Idle;
    QGraphicsScene*      m_scene        = nullptr;
    QGraphicsRectItem*   m_bgRectItem   = nullptr;
    QGraphicsPixmapItem* m_pixmapItem   = nullptr;
    QLabel*              m_overlayLabel = nullptr;
    QLabel*              m_bannerLabel  = nullptr;
};
