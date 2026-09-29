#include "SoundFilterModel.h"

#include "SoundLibrary.h"

namespace sb {

SoundFilterModel::SoundFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(SoundLibrary::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

template <typename Apply>
void SoundFilterModel::changeFilter(Apply apply)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    apply();
    endFilterChange(Direction::Rows);
#else
    apply();
    invalidateRowsFilter();
#endif
}

void SoundFilterModel::setCategory(int categoryId)
{
    if (categoryId != m_category)
        changeFilter([&] { m_category = categoryId; });
}

void SoundFilterModel::setSearchText(const QString &text)
{
    if (text == m_search)
        return;
    changeFilter([&] {
        m_search = text;
        m_searchWords = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    });
}

bool SoundFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_category != AllCategories
        && index.data(SoundLibrary::CategoryIdRole).toInt() != m_category)
        return false;
    if (m_searchWords.isEmpty())
        return true;
    const QString haystack = index.data(SoundLibrary::SearchRole).toString();
    for (const QString &word : m_searchWords) {
        if (!haystack.contains(word, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

} // namespace sb
