#include "Icons.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QtMath>

#include <cmath>

namespace sb::icons {

namespace {

constexpr QColor kRecordRed(229, 57, 53);
constexpr QColor kWarningAmber(240, 163, 10);

QPainterPath arrowHead(QPointF tip, double angleDegrees, double size)
{
    // A filled triangle pointing along `angleDegrees` (0 = right, 90 = down).
    const double a = qDegreesToRadians(angleDegrees);
    const QPointF back(tip.x() - size * std::cos(a), tip.y() - size * std::sin(a));
    const QPointF normal(-std::sin(a) * size * 0.6, std::cos(a) * size * 0.6);
    QPainterPath path;
    path.moveTo(tip);
    path.lineTo(back + normal);
    path.lineTo(back - normal);
    path.closeSubpath();
    return path;
}

// Draws `glyph` on a 24x24 grid.
void drawGlyph(QPainter &p, Glyph glyph, const QColor &color)
{
    QPen stroke(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    switch (glyph) {
    case Glyph::Play: {
        QPainterPath path;
        path.moveTo(7.5, 4.5);
        path.lineTo(19.5, 12);
        path.lineTo(7.5, 19.5);
        path.closeSubpath();
        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        break;
    }
    case Glyph::Pause:
        p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
        break;
    case Glyph::Stop:
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
        break;
    case Glyph::Previous:
    case Glyph::Next: {
        if (glyph == Glyph::Next) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawRoundedRect(QRectF(5, 6, 2.5, 12), 1, 1);
        QPainterPath path;
        path.moveTo(19, 6);
        path.lineTo(9, 12);
        path.lineTo(19, 18);
        path.closeSubpath();
        p.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        break;
    }
    case Glyph::Add:
        stroke.setWidthF(2.4);
        p.setPen(stroke);
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Glyph::Remove:
        stroke.setWidthF(2.4);
        p.setPen(stroke);
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Glyph::Folder:
    case Glyph::AddFolder: {
        QPainterPath path;
        path.moveTo(3, 7);
        path.lineTo(3, 18.5);
        path.lineTo(21, 18.5);
        path.lineTo(21, 8.5);
        path.lineTo(11.5, 8.5);
        path.lineTo(9.5, 5.5);
        path.lineTo(3.5, 5.5);
        path.closeSubpath();
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QColor fill = color;
        fill.setAlphaF(0.25);
        p.setBrush(fill);
        p.drawPath(path);
        if (glyph == Glyph::AddFolder) {
            p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(12, 10.5), QPointF(12, 16.5));
            p.drawLine(QPointF(9, 13.5), QPointF(15, 13.5));
        }
        break;
    }
    case Glyph::Record:
        p.setBrush(kRecordRed);
        p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        break;
    case Glyph::Speaker: {
        QPainterPath body;
        body.moveTo(3.5, 9);
        body.lineTo(7.5, 9);
        body.lineTo(12.5, 4.5);
        body.lineTo(12.5, 19.5);
        body.lineTo(7.5, 15);
        body.lineTo(3.5, 15);
        body.closeSubpath();
        p.drawPath(body);
        stroke.setWidthF(1.8);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(9.5, 8, 8, 8), -50 * 16, 100 * 16);
        p.drawArc(QRectF(8, 4.5, 14, 15), -50 * 16, 100 * 16);
        break;
    }
    case Glyph::Microphone: {
        p.drawRoundedRect(QRectF(8.5, 2.5, 7, 12), 3.5, 3.5);
        stroke.setWidthF(1.8);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(5.5, 5, 13, 12.5), 180 * 16, 180 * 16);
        p.drawLine(QPointF(12, 17.5), QPointF(12, 20.5));
        p.drawLine(QPointF(8.5, 20.5), QPointF(15.5, 20.5));
        break;
    }
    case Glyph::Settings: {
        QPainterPath gear;
        gear.addEllipse(QPointF(12, 12), 6.3, 6.3);
        for (int i = 0; i < 8; ++i) {
            QPainterPath tooth;
            tooth.addRoundedRect(QRectF(-1.7, -10, 3.4, 5), 0.8, 0.8);
            QTransform t;
            t.translate(12, 12);
            t.rotate(i * 45.0);
            gear = gear.united(t.map(tooth));
        }
        QPainterPath hole;
        hole.addEllipse(QPointF(12, 12), 2.8, 2.8);
        p.drawPath(gear.subtracted(hole));
        break;
    }
    case Glyph::Search:
        stroke.setWidthF(2.2);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        p.drawLine(QPointF(14.2, 14.2), QPointF(19.5, 19.5));
        break;
    case Glyph::Loop: {
        stroke.setWidthF(2.0);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(5, 13);
        path.lineTo(5, 10.5);
        path.arcTo(QRectF(5, 6.5, 6, 6), 180, -90);
        path.lineTo(16, 6.5);
        p.drawPath(path);
        QPainterPath back;
        back.moveTo(19, 11);
        back.lineTo(19, 13.5);
        back.arcTo(QRectF(13, 11.5, 6, 6), 0, -90);
        back.lineTo(8, 17.5);
        p.drawPath(back);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(arrowHead(QPointF(20, 6.5), 0, 4.5));
        p.drawPath(arrowHead(QPointF(4, 17.5), 180, 4.5));
        break;
    }
    case Glyph::Shuffle: {
        stroke.setWidthF(2.0);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        QPainterPath a;
        a.moveTo(3.5, 7);
        a.cubicTo(10, 7, 12, 17, 17, 17);
        QPainterPath b;
        b.moveTo(3.5, 17);
        b.cubicTo(10, 17, 12, 7, 17, 7);
        p.drawPath(a);
        p.drawPath(b);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(arrowHead(QPointF(21, 7), 0, 4.5));
        p.drawPath(arrowHead(QPointF(21, 17), 0, 4.5));
        break;
    }
    case Glyph::Tag: {
        QPainterPath path;
        path.moveTo(4, 4);
        path.lineTo(12, 4);
        path.lineTo(20.5, 12.5);
        path.lineTo(12.5, 20.5);
        path.lineTo(4, 12);
        path.closeSubpath();
        QPainterPath hole;
        hole.addEllipse(QPointF(8.5, 8.5), 1.7, 1.7);
        p.drawPath(path.subtracted(hole));
        break;
    }
    case Glyph::Edit: {
        QPainterPath path;
        path.moveTo(15, 4.5);
        path.lineTo(19.5, 9);
        path.lineTo(9, 19.5);
        path.lineTo(4.5, 19.5);
        path.lineTo(4.5, 15);
        path.closeSubpath();
        p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.drawLine(QPointF(12.5, 7), QPointF(17, 11.5));
        break;
    }
    case Glyph::Keyboard: {
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 6, 19, 12), 2, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 6; ++col)
                p.drawRect(QRectF(5 + col * 2.6, 8.5 + row * 2.6, 1.6, 1.6));
        }
        p.drawRoundedRect(QRectF(7.5, 14, 9, 1.8), 0.6, 0.6);
        break;
    }
    case Glyph::Warning: {
        QPainterPath triangle;
        triangle.moveTo(12, 3);
        triangle.lineTo(22, 20.5);
        triangle.lineTo(2, 20.5);
        triangle.closeSubpath();
        p.setPen(QPen(kWarningAmber, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(kWarningAmber);
        p.drawPath(triangle);
        p.setPen(QPen(Qt::black, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(12, 9), QPointF(12, 14));
        p.drawPoint(QPointF(12, 17.3));
        break;
    }
    case Glyph::Info:
        p.setPen(QPen(color, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.drawPoint(QPointF(12, 7.5));
        break;
    case Glyph::Quit:
        stroke.setWidthF(2.2);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(5, 5.5, 14, 14), 120 * 16, -240 * 16);
        p.drawLine(QPointF(12, 3.5), QPointF(12, 11));
        break;
    }
}

class GlyphEngine : public QIconEngine {
public:
    explicit GlyphEngine(Glyph glyph, bool slashWhenOff = false)
        : m_glyph(glyph)
        , m_slashWhenOff(slashWhenOff)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const QPalette palette = QGuiApplication::palette();
        QColor color = palette.color(QPalette::Active, QPalette::WindowText);
        if (mode == QIcon::Disabled)
            color = palette.color(QPalette::Disabled, QPalette::WindowText);
        else if (mode == QIcon::Selected)
            color = palette.color(QPalette::Active, QPalette::HighlightedText);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const double side = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0,
                           rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        const bool off = m_slashWhenOff && state == QIcon::Off;
        if (mode == QIcon::Disabled || off)
            painter->setOpacity(0.55);
        drawGlyph(*painter, m_glyph, color);
        if (off) {
            painter->setOpacity(1.0);
            painter->setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(4, 4), QPointF(20, 20));
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(scale);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine *clone() const override { return new GlyphEngine(m_glyph, m_slashWhenOff); }
    QString key() const override { return QStringLiteral("sb-glyph"); }

private:
    Glyph m_glyph;
    bool m_slashWhenOff;
};

} // namespace

QIcon icon(Glyph glyph)
{
    return QIcon(new GlyphEngine(glyph));
}

QIcon toggleIcon(Glyph glyph)
{
    return QIcon(new GlyphEngine(glyph, true));
}

QImage appIconImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 64.0, size / 64.0);

    QLinearGradient background(0, 0, 64, 64);
    background.setColorAt(0, QColor(124, 77, 255));
    background.setColorAt(1, QColor(41, 121, 255));
    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(QRectF(3, 3, 58, 58), 14, 14);

    // Equaliser bars.
    p.setBrush(Qt::white);
    const double heights[] = {14, 26, 38, 26, 14};
    for (int i = 0; i < 5; ++i) {
        const double h = heights[i];
        p.drawRoundedRect(QRectF(13 + i * 8.5, 32 - h / 2, 5, h), 2.5, 2.5);
    }
    return image;
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(QPixmap::fromImage(appIconImage(size)));
    return icon;
}

} // namespace sb::icons
