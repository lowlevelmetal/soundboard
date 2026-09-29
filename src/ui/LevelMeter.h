#pragma once

#include <QWidget>

namespace sb {

// A small horizontal peak meter with a slow fall-off.
class LevelMeter : public QWidget {
    Q_OBJECT

public:
    explicit LevelMeter(QWidget *parent = nullptr);

    // Linear peak, 0..1.
    void setLevel(float peak);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_display = 0.0f; // 0..1 on a dB scale
};

} // namespace sb
