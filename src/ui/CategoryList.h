#pragma once

#include <QListWidget>

namespace sb {

struct Category;

// The category sidebar: "All sounds", "Uncategorized" and user categories.
// Sounds can be dropped onto a category to move them there.
class CategoryList : public QListWidget {
    Q_OBJECT

public:
    static constexpr int AllSounds = -1;
    static constexpr int Uncategorized = 0;

    explicit CategoryList(QWidget *parent = nullptr);

    void setCategories(const QList<Category> &categories);
    int currentCategory() const;
    void setCurrentCategory(int id);

signals:
    void categorySelected(int id);
    void soundsDropped(const QList<int> &soundIds, int categoryId);
    void categoryRenamed(int id, const QString &name);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    int categoryAt(const QPoint &pos) const;
};

} // namespace sb
