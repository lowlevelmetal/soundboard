#include "SoundTableView.h"

#include "library/SoundLibrary.h"

#include <QDragEnterEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QPainter>

namespace sb {

SoundTableView::SoundTableView(QWidget *parent)
    : QTableView(parent)
{
    setSelectionBehavior(SelectRows);
    setSelectionMode(ExtendedSelection);
    setEditTriggers(EditKeyPressed);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setWordWrap(false);
    setSortingEnabled(true);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false); // drawn in paintEvent instead
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 10);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setSectionsMovable(true);
    horizontalHeader()->setStretchLastSection(true);
}

bool SoundTableView::acceptsDrag(const QMimeData *mime) const
{
    if (mime->hasUrls())
        return true;
    return m_reorderEnabled && mime->hasFormat(SoundLibrary::MimeType);
}

int SoundTableView::dropRow(const QPoint &pos) const
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid())
        return model() ? model()->rowCount() : 0;
    const QRect rect = visualRect(index);
    return pos.y() > rect.center().y() ? index.row() + 1 : index.row();
}

void SoundTableView::dragEnterEvent(QDragEnterEvent *event)
{
    if (acceptsDrag(event->mimeData())) {
        event->setDropAction(event->mimeData()->hasUrls() ? Qt::CopyAction : Qt::MoveAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void SoundTableView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrag(event->mimeData())) {
        event->ignore();
        return;
    }
    const bool internal = !event->mimeData()->hasUrls();
    event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    const int row = internal ? dropRow(event->position().toPoint()) : -1;
    if (row != m_indicatorRow) {
        m_indicatorRow = row;
        viewport()->update();
    }
    // Let the base class auto-scroll near the edges.
    QAbstractItemView::dragMoveEvent(event);
    event->setDropAction(internal ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
}

void SoundTableView::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_indicatorRow = -1;
    viewport()->update();
    QTableView::dragLeaveEvent(event);
}

void SoundTableView::dropEvent(QDropEvent *event)
{
    m_indicatorRow = -1;
    viewport()->update();
    const QMimeData *mime = event->mimeData();
    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        }
        event->setDropAction(Qt::CopyAction);
        event->accept();
        if (!paths.isEmpty())
            emit filesDropped(paths);
        return;
    }
    if (!m_reorderEnabled || !mime->hasFormat(SoundLibrary::MimeType)) {
        event->ignore();
        return;
    }
    const QList<int> ids = SoundLibrary::idsFromMimeData(mime);
    const int row = dropRow(event->position().toPoint());
    int beforeId = 0;
    // Skip over sounds that are themselves being moved.
    for (int r = row; r < model()->rowCount(); ++r) {
        const int id = model()->index(r, 0).data(SoundLibrary::IdRole).toInt();
        if (!ids.contains(id)) {
            beforeId = id;
            break;
        }
    }
    // Accept as a copy so the view doesn't try to remove the dragged rows itself.
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit soundsReordered(ids, beforeId);
}

void SoundTableView::paintEvent(QPaintEvent *event)
{
    QTableView::paintEvent(event);
    if (m_indicatorRow < 0 || !model())
        return;
    int y = 0;
    if (m_indicatorRow < model()->rowCount())
        y = rowViewportPosition(m_indicatorRow);
    else if (model()->rowCount() > 0)
        y = rowViewportPosition(model()->rowCount() - 1) + rowHeight(model()->rowCount() - 1);
    QPainter p(viewport());
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.drawLine(0, y, viewport()->width(), y);
}

} // namespace sb
