#pragma once

#include <QTableView>

namespace sb {

// The sound list. Accepts audio files dropped from a file manager and lets
// sounds be dragged to reorder them or onto a category.
class SoundTableView : public QTableView {
    Q_OBJECT

public:
    explicit SoundTableView(QWidget *parent = nullptr);

    void setReorderEnabled(bool enabled) { m_reorderEnabled = enabled; }

signals:
    void filesDropped(const QStringList &paths);
    // Move `ids` before `beforeId`; 0 means to the end.
    void soundsReordered(const QList<int> &ids, int beforeId);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool acceptsDrag(const QMimeData *mime) const;
    // Row the dragged sounds would be inserted before (rowCount() for "at the end").
    int dropRow(const QPoint &pos) const;

    bool m_reorderEnabled = true;
    int m_indicatorRow = -1;
};

} // namespace sb
