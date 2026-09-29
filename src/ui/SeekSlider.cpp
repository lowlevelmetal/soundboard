#include "SeekSlider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

namespace sb {

SeekSlider::SeekSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setTracking(false);
    connect(this, &QSlider::sliderReleased, this, [this] { emit seekRequested(sliderPosition()); });
    connect(this, &QSlider::actionTriggered, this, [this](int action) {
        // Keyboard and page-step moves seek immediately.
        if (action != SliderMove && action != SliderNoAction)
            emit seekRequested(sliderPosition());
    });
}

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && maximum() > minimum()) {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect handle =
            style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        if (!handle.contains(event->position().toPoint())) {
            const int span = width() - handle.width();
            const int x = qRound(event->position().x()) - handle.width() / 2;
            const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), x, span,
                                                              option.upsideDown);
            setSliderPosition(value);
            emit seekRequested(value);
        }
    }
    QSlider::mousePressEvent(event);
}

} // namespace sb
