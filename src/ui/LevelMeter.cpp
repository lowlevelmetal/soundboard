#include "LevelMeter.h"

#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace sb {

namespace {

constexpr float kFloorDb = -60.0f;

} // namespace

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void LevelMeter::setLevel(float peak)
{
    const float db = peak > 0.0f ? 20.0f * std::log10(peak) : kFloorDb;
    const float target = std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
    // Rise instantly, fall slowly.
    const float next = target >= m_display ? target : std::max(target, m_display - 0.04f);
    if (std::fabs(next - m_display) > 0.001f) {
        m_display = next;
        update();
    }
}

QSize LevelMeter::sizeHint() const
{
    return {80, 6};
}

QSize LevelMeter::minimumSizeHint() const
{
    return {30, 6};
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor background = palette().color(QPalette::WindowText);
    background.setAlphaF(0.12f);
    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(track, 2, 2);

    if (m_display <= 0.0f)
        return;
    QRectF bar = track;
    bar.setWidth(track.width() * m_display);
    QColor color(67, 160, 71); // green
    if (m_display > 0.93f)
        color = QColor(229, 57, 53); // clipping
    else if (m_display > 0.8f)
        color = QColor(240, 163, 10);
    p.setBrush(color);
    p.drawRoundedRect(bar, 2, 2);
}

} // namespace sb
