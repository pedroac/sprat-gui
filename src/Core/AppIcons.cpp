#include "AppIcons.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmapCache>
#include <QSvgRenderer>

namespace {

/// Draws an SVG with "currentColor" replaced by the palette colour for the icon's mode.
class ThemedSvgIconEngine : public QIconEngine {
public:
    ThemedSvgIconEngine(QString path, QByteArray svg) : m_path(std::move(path)), m_svg(std::move(svg)) {}

    QIconEngine* clone() const override { return new ThemedSvgIconEngine(m_path, m_svg); }
    QString key() const override { return QStringLiteral("ThemedSvgIconEngine"); }
    bool isNull() override { return m_svg.isEmpty(); }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        QSvgRenderer renderer(coloured(mode));
        renderer.render(painter, QRectF(rect));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        const QColor colour = colourFor(mode);
        const QString cacheKey = QStringLiteral("appicon:%1:%2x%3@%4:%5")
                                     .arg(m_path).arg(size.width()).arg(size.height()).arg(scale).arg(colour.name(QColor::HexArgb));
        QPixmap pixmap;
        if (QPixmapCache::find(cacheKey, &pixmap)) return pixmap;
        pixmap = QPixmap(size * scale);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            QSvgRenderer renderer(coloured(mode));
            renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size * scale)));
        }
        pixmap.setDevicePixelRatio(scale);
        QPixmapCache::insert(cacheKey, pixmap);
        return pixmap;
    }

private:
    static QColor colourFor(QIcon::Mode mode) {
        const QPalette palette = QGuiApplication::palette();
        switch (mode) {
        case QIcon::Disabled: return palette.color(QPalette::Disabled, QPalette::WindowText);
        case QIcon::Selected: return palette.color(QPalette::HighlightedText);
        default: return palette.color(QPalette::WindowText);
        }
    }

    QByteArray coloured(QIcon::Mode mode) const {
        QByteArray svg = m_svg;
        QColor colour = colourFor(mode);
        svg.replace("currentColor", colour.name(QColor::HexRgb).toLatin1());
        // A disabled icon's own colours (warning amber, error red) are faded too.
        if (mode == QIcon::Disabled) svg.replace("<svg ", "<svg opacity=\"0.5\" ");
        return svg;
    }

    QString m_path;
    QByteArray m_svg;
};

}  // namespace

namespace AppIcons {

QIcon icon(const QString& name) {
    static QHash<QString, QByteArray> sources;
    auto it = sources.constFind(name);
    if (it == sources.constEnd()) {
        QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
        QByteArray svg;
        if (file.open(QIODevice::ReadOnly)) svg = file.readAll();
        else qWarning("AppIcons: no icon named \"%s\"", qPrintable(name));
        it = sources.insert(name, svg);
    }
    if (it->isEmpty()) return {};
    return QIcon(new ThemedSvgIconEngine(name, *it));
}

}  // namespace AppIcons
