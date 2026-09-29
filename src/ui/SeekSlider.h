#pragma once

#include <QSlider>

namespace sb {

// A horizontal slider that jumps straight to the clicked position and only
// reports a seek when the user lets go.
class SeekSlider : public QSlider {
    Q_OBJECT

public:
    explicit SeekSlider(QWidget *parent = nullptr);

signals:
    void seekRequested(int value);

protected:
    void mousePressEvent(QMouseEvent *event) override;
};

} // namespace sb
