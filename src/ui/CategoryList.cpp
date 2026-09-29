#include "CategoryList.h"

#include "Icons.h"
#include "library/SoundLibrary.h"

#include <QDragEnterEvent>
#include <QMimeData>

namespace sb {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

} // namespace

CategoryList::CategoryList(QWidget *parent)
    : QListWidget(parent)
{
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(DropOnly);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSelectionMode(SingleSelection);
    connect(this, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            emit categorySelected(item->data(kIdRole).toInt());
    });
    connect(this, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int id = item->data(kIdRole).toInt();
        if (id > 0 && !item->text().trimmed().isEmpty())
            emit categoryRenamed(id, item->text().trimmed());
    });
}

void CategoryList::setCategories(const QList<Category> &categories)
{
    const int current = currentCategory();
    const QSignalBlocker blocker(this);
    clear();
    auto add = [this](const QString &name, int id, icons::Glyph glyph) {
        auto *item = new QListWidgetItem(icons::icon(glyph), name, this);
        item->setData(kIdRole, id);
        if (id > 0)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
    };
    add(tr("All sounds"), AllSounds, icons::Glyph::Speaker);
    add(tr("Uncategorized"), Uncategorized, icons::Glyph::Folder);
    for (const Category &category : categories)
        add(category.name, category.id, icons::Glyph::Tag);
    setCurrentCategory(current);
    if (!currentItem())
        setCurrentRow(0);
}

int CategoryList::currentCategory() const
{
    const QListWidgetItem *item = currentItem();
    return item ? item->data(kIdRole).toInt() : AllSounds;
}

void CategoryList::setCurrentCategory(int id)
{
    for (int row = 0; row < count(); ++row) {
        if (item(row)->data(kIdRole).toInt() == id) {
            setCurrentRow(row);
            return;
        }
    }
}

int CategoryList::categoryAt(const QPoint &pos) const
{
    const QListWidgetItem *target = itemAt(pos);
    return target ? target->data(kIdRole).toInt() : AllSounds;
}

void CategoryList::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(SoundLibrary::MimeType))
        event->acceptProposedAction();
    else
        event->ignore();
}

void CategoryList::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(SoundLibrary::MimeType)
        && categoryAt(event->position().toPoint()) != AllSounds) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void CategoryList::dropEvent(QDropEvent *event)
{
    const int category = categoryAt(event->position().toPoint());
    const QList<int> ids = SoundLibrary::idsFromMimeData(event->mimeData());
    if (category == AllSounds || ids.isEmpty()) {
        event->ignore();
        return;
    }
    // A copy action keeps the source view from deleting anything.
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit soundsDropped(ids, category);
}

} // namespace sb
