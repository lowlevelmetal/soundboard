#pragma once

#include <QSortFilterProxyModel>

namespace sb {

// Filters the sound table by category and search text.
class SoundFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    static constexpr int AllCategories = -1;

    explicit SoundFilterModel(QObject *parent = nullptr);

    int category() const { return m_category; }
    void setCategory(int categoryId);
    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    template <typename Apply>
    void changeFilter(Apply apply);

    int m_category = AllCategories;
    QString m_search;
    QStringList m_searchWords;
};

} // namespace sb
