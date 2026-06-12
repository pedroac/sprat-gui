#include "SpritePreviewTooltip.h"
#include "ViewUtils.h"
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QApplication>

SpritePreviewTooltip::SpritePreviewTooltip(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void SpritePreviewTooltip::showAt(const QString& spritePath, const QPoint& globalPos) {
    const QPixmap raw(spritePath);
    if (raw.isNull()) { hide(); return; }

    m_pixmap = (raw.width() > kMaxSize || raw.height() > kMaxSize)
        ? raw.scaled(kMaxSize, kMaxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : raw;

    const int w = m_pixmap.width()  + kPadding * 2;
    const int h = m_pixmap.height() + kPadding * 2;
    resize(w, h);

    QPoint pos = globalPos + QPoint(kCursorOffset, kCursorOffset);
    const QScreen* screen = QApplication::screenAt(globalPos);
    if (screen) {
        const QRect sg = screen->geometry();
        if (pos.x() + w > sg.right())  pos.setX(globalPos.x() - w - kCursorOffset);
        if (pos.y() + h > sg.bottom()) pos.setY(globalPos.y() - h - kCursorOffset);
    }
    move(pos);
    show();
    update();
}

void SpritePreviewTooltip::paintEvent(QPaintEvent*) {
    QPainter p(this);

    const QRect imgRect(kPadding, kPadding, m_pixmap.width(), m_pixmap.height());

    // Background surrounding the image
    p.fillRect(rect(), QColor(45, 45, 45));
    // Checkerboard under the sprite for transparency
    p.drawTiledPixmap(imgRect, createCheckerboardPixmap());
    // Sprite
    p.drawPixmap(imgRect.topLeft(), m_pixmap);
    // Border
    p.setPen(QColor(90, 90, 90));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}
