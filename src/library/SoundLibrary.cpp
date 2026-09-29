#include "SoundLibrary.h"

#include "ui/Icons.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QMimeData>
#include <QPalette>
#include <QApplication>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace sb {

const QString SoundLibrary::MimeType = QStringLiteral("application/x-soundboard-sound-ids");

namespace {

constexpr int kFormatVersion = 1;

QString hotkeyDisplayText(const QString &portable)
{
    if (portable.isEmpty())
        return {};
    return QKeySequence(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

} // namespace

SoundLibrary::SoundLibrary(const QString &filePath, QObject *parent)
    : QAbstractTableModel(parent)
    , m_filePath(filePath)
    , m_saveTimer(new QTimer(this))
{
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { save(); });
}

SoundLibrary::~SoundLibrary()
{
    if (m_saveTimer->isActive())
        save();
}

bool SoundLibrary::load(QString *error)
{
    QFile file(m_filePath);
    if (!file.exists())
        return true; // first run
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Could not open %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        if (error)
            *error = tr("%1 is not valid JSON: %2").arg(m_filePath, parseError.errorString());
        return false;
    }

    beginResetModel();
    m_sounds.clear();
    m_categories.clear();
    const QJsonObject root = doc.object();
    for (const QJsonValue &value : root.value(QLatin1String("categories")).toArray()) {
        const QJsonObject o = value.toObject();
        Category category;
        category.id = o.value(QLatin1String("id")).toInt();
        category.name = o.value(QLatin1String("name")).toString();
        if (category.id <= 0)
            continue;
        m_categories.append(category);
        m_nextCategoryId = std::max(m_nextCategoryId, category.id + 1);
    }
    for (const QJsonValue &value : root.value(QLatin1String("sounds")).toArray()) {
        const QJsonObject o = value.toObject();
        Sound sound;
        sound.id = o.value(QLatin1String("id")).toInt();
        sound.path = o.value(QLatin1String("path")).toString();
        if (sound.id <= 0 || sound.path.isEmpty())
            continue;
        sound.title = o.value(QLatin1String("title")).toString();
        if (sound.title.isEmpty())
            sound.title = QFileInfo(sound.path).completeBaseName();
        sound.hotkey = o.value(QLatin1String("hotkey")).toString();
        sound.categoryId = o.value(QLatin1String("category")).toInt();
        if (categoryName(sound.categoryId).isNull())
            sound.categoryId = 0;
        sound.volume = std::clamp(o.value(QLatin1String("volume")).toInt(100), 0, 200);
        sound.durationMs = o.value(QLatin1String("durationMs")).toInteger(-1);
        sound.playCount = o.value(QLatin1String("playCount")).toInt();
        sound.addedAt = QDateTime::fromString(o.value(QLatin1String("added")).toString(),
                                              Qt::ISODate);
        sound.lastPlayedAt = QDateTime::fromString(
            o.value(QLatin1String("lastPlayed")).toString(), Qt::ISODate);
        m_sounds.append(sound);
        m_nextSoundId = std::max(m_nextSoundId, sound.id + 1);
    }
    endResetModel();
    refreshMissing();
    emit categoriesChanged();
    emit hotkeysChanged();
    return true;
}

bool SoundLibrary::save(QString *error)
{
    m_saveTimer->stop();
    QJsonArray categories;
    for (const Category &category : std::as_const(m_categories))
        categories.append(QJsonObject{{QLatin1String("id"), category.id},
                                      {QLatin1String("name"), category.name}});
    QJsonArray sounds;
    for (const Sound &sound : std::as_const(m_sounds)) {
        QJsonObject o{{QLatin1String("id"), sound.id},
                      {QLatin1String("path"), sound.path},
                      {QLatin1String("title"), sound.title},
                      {QLatin1String("volume"), sound.volume},
                      {QLatin1String("durationMs"), sound.durationMs},
                      {QLatin1String("playCount"), sound.playCount},
                      {QLatin1String("added"), sound.addedAt.toString(Qt::ISODate)}};
        if (!sound.hotkey.isEmpty())
            o.insert(QLatin1String("hotkey"), sound.hotkey);
        if (sound.categoryId != 0)
            o.insert(QLatin1String("category"), sound.categoryId);
        if (sound.lastPlayedAt.isValid())
            o.insert(QLatin1String("lastPlayed"), sound.lastPlayedAt.toString(Qt::ISODate));
        sounds.append(o);
    }
    const QJsonObject root{{QLatin1String("version"), kFormatVersion},
                           {QLatin1String("categories"), categories},
                           {QLatin1String("sounds"), sounds}};

    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void SoundLibrary::scheduleSave()
{
    m_saveTimer->start();
}

int SoundLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sounds.size();
}

int SoundLibrary::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SoundLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sounds.size())
        return {};
    const Sound &sound = m_sounds.at(index.row());
    const int column = index.column();

    switch (role) {
    case IdRole:
        return sound.id;
    case CategoryIdRole:
        return sound.categoryId;
    case SearchRole:
        return sound.title + QLatin1Char('\n') + QFileInfo(sound.path).fileName();
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (column) {
        case IndexColumn:
            return index.row() + 1;
        case TitleColumn:
            return sound.title;
        case HotkeyColumn:
            return hotkeyDisplayText(sound.hotkey);
        case DurationColumn:
            return formatDuration(sound.durationMs);
        case CategoryColumn:
            return categoryName(sound.categoryId);
        case PlaysColumn:
            return sound.playCount;
        case AddedColumn:
            return QLocale().toString(sound.addedAt.date(), QLocale::ShortFormat);
        case PathColumn:
            return QDir::toNativeSeparators(sound.path);
        }
        break;
    case SortRole:
        switch (column) {
        case IndexColumn:
            return index.row();
        case TitleColumn:
            return sound.title.toLower();
        case HotkeyColumn:
            return hotkeyDisplayText(sound.hotkey);
        case DurationColumn:
            return sound.durationMs;
        case CategoryColumn:
            return categoryName(sound.categoryId).toLower();
        case PlaysColumn:
            return sound.playCount;
        case AddedColumn:
            return sound.addedAt;
        case PathColumn:
            return sound.path.toLower();
        }
        break;
    case Qt::TextAlignmentRole:
        if (column == IndexColumn || column == DurationColumn || column == PlaysColumn)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case Qt::DecorationRole:
        if (column == TitleColumn && m_playing.contains(sound.id))
            return icons::icon(icons::Glyph::Speaker);
        if (column == HotkeyColumn && m_hotkeyErrors.contains(sound.id))
            return icons::icon(icons::Glyph::Warning);
        if (column == TitleColumn && m_missing.contains(sound.id))
            return icons::icon(icons::Glyph::Warning);
        break;
    case Qt::FontRole:
        if (m_playing.contains(sound.id)) {
            QFont font;
            font.setBold(true);
            return font;
        }
        break;
    case Qt::ForegroundRole:
        if (m_missing.contains(sound.id))
            return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        break;
    case Qt::ToolTipRole:
        if (column == HotkeyColumn && m_hotkeyErrors.contains(sound.id))
            return m_hotkeyErrors.value(sound.id);
        if (column == HotkeyColumn && m_hotkeyTriggers.contains(sound.id))
            return tr("Desktop shortcut: %1").arg(m_hotkeyTriggers.value(sound.id));
        if (m_missing.contains(sound.id))
            return tr("File not found: %1").arg(QDir::toNativeSeparators(sound.path));
        if (column == TitleColumn || column == PathColumn)
            return QDir::toNativeSeparators(sound.path);
        break;
    }
    return {};
}

QVariant SoundLibrary::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole
        && (section == IndexColumn || section == DurationColumn || section == PlaysColumn))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case IndexColumn:
        return tr("#");
    case TitleColumn:
        return tr("Title");
    case HotkeyColumn:
        return tr("Hotkey");
    case DurationColumn:
        return tr("Duration");
    case CategoryColumn:
        return tr("Category");
    case PlaysColumn:
        return tr("Played");
    case AddedColumn:
        return tr("Added");
    case PathColumn:
        return tr("File");
    }
    return {};
}

Qt::ItemFlags SoundLibrary::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return f;
    f |= Qt::ItemIsDragEnabled;
    if (index.column() == TitleColumn)
        f |= Qt::ItemIsEditable;
    return f;
}

bool SoundLibrary::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != TitleColumn)
        return false;
    const QString title = value.toString().trimmed();
    if (title.isEmpty())
        return false;
    setTitle(m_sounds.at(index.row()).id, title);
    return true;
}

QStringList SoundLibrary::mimeTypes() const
{
    return {MimeType};
}

QMimeData *SoundLibrary::mimeData(const QModelIndexList &indexes) const
{
    QList<int> ids;
    for (const QModelIndex &index : indexes) {
        const int id = index.data(IdRole).toInt();
        if (!ids.contains(id))
            ids.append(id);
    }
    QStringList parts;
    for (int id : std::as_const(ids))
        parts.append(QString::number(id));
    auto *mime = new QMimeData;
    mime->setData(MimeType, parts.join(QLatin1Char(',')).toLatin1());
    return mime;
}

Qt::DropActions SoundLibrary::supportedDragActions() const
{
    return Qt::MoveAction;
}

QList<int> SoundLibrary::idsFromMimeData(const QMimeData *mime)
{
    QList<int> ids;
    if (!mime || !mime->hasFormat(MimeType))
        return ids;
    const QList<QByteArray> parts = mime->data(MimeType).split(',');
    for (const QByteArray &part : parts) {
        bool ok = false;
        const int id = part.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    return ids;
}

QString SoundLibrary::formatDuration(qint64 ms)
{
    if (ms < 0)
        return {};
    const qint64 totalSeconds = (ms + 500) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

const Sound *SoundLibrary::find(int id) const
{
    const int row = rowOf(id);
    return row >= 0 ? &m_sounds.at(row) : nullptr;
}

int SoundLibrary::rowOf(int id) const
{
    for (int row = 0; row < m_sounds.size(); ++row) {
        if (m_sounds.at(row).id == id)
            return row;
    }
    return -1;
}

QList<int> SoundLibrary::addFiles(const QStringList &paths, int categoryId, int *skipped)
{
    QSet<QString> existing;
    for (const Sound &sound : std::as_const(m_sounds))
        existing.insert(QFileInfo(sound.path).absoluteFilePath());

    QList<Sound> added;
    int skippedCount = 0;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString absolute = info.absoluteFilePath();
        if (existing.contains(absolute)) {
            ++skippedCount;
            continue;
        }
        existing.insert(absolute);
        Sound sound;
        sound.id = m_nextSoundId++;
        sound.path = absolute;
        sound.title = info.completeBaseName();
        sound.categoryId = categoryName(categoryId).isNull() ? 0 : categoryId;
        sound.addedAt = now;
        added.append(sound);
    }
    if (skipped)
        *skipped = skippedCount;
    if (added.isEmpty())
        return {};

    beginInsertRows({}, m_sounds.size(), m_sounds.size() + added.size() - 1);
    m_sounds.append(added);
    endInsertRows();

    QList<int> ids;
    for (const Sound &sound : std::as_const(added)) {
        ids.append(sound.id);
        if (!QFileInfo::exists(sound.path))
            m_missing.insert(sound.id);
    }
    scheduleSave();
    return ids;
}

void SoundLibrary::removeSounds(const QList<int> &ids)
{
    bool hadHotkey = false;
    for (int id : ids) {
        const int row = rowOf(id);
        if (row < 0)
            continue;
        hadHotkey = hadHotkey || !m_sounds.at(row).hotkey.isEmpty();
        beginRemoveRows({}, row, row);
        m_sounds.removeAt(row);
        endRemoveRows();
        m_missing.remove(id);
        m_playing.remove(id);
        m_hotkeyErrors.remove(id);
    }
    // Row numbers shift, so the "#" column changes for everything below.
    if (!m_sounds.isEmpty())
        emit dataChanged(index(0, IndexColumn), index(m_sounds.size() - 1, IndexColumn));
    scheduleSave();
    if (hadHotkey)
        emit hotkeysChanged();
}

void SoundLibrary::soundChanged(int id, const QList<int> &roles)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1), roles);
}

void SoundLibrary::setTitle(int id, const QString &title)
{
    const int row = rowOf(id);
    if (row < 0 || title.isEmpty() || m_sounds[row].title == title)
        return;
    m_sounds[row].title = title;
    soundChanged(id);
    scheduleSave();
    if (!m_sounds[row].hotkey.isEmpty())
        emit hotkeysChanged(); // hotkey descriptions include the title
}

void SoundLibrary::setHotkey(int id, const QString &portableHotkey)
{
    const int row = rowOf(id);
    if (row < 0 || m_sounds[row].hotkey == portableHotkey)
        return;
    m_sounds[row].hotkey = portableHotkey;
    m_hotkeyErrors.remove(id);
    soundChanged(id);
    scheduleSave();
    emit hotkeysChanged();
}

void SoundLibrary::setCategory(const QList<int> &ids, int categoryId)
{
    if (categoryId != 0 && categoryName(categoryId).isNull())
        return;
    for (int id : ids) {
        const int row = rowOf(id);
        if (row < 0 || m_sounds[row].categoryId == categoryId)
            continue;
        m_sounds[row].categoryId = categoryId;
        soundChanged(id);
    }
    scheduleSave();
}

void SoundLibrary::setVolume(int id, int percent)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_sounds[row].volume = std::clamp(percent, 0, 200);
    scheduleSave();
}

void SoundLibrary::setDuration(int id, qint64 durationMs)
{
    const int row = rowOf(id);
    if (row < 0 || m_sounds[row].durationMs == durationMs)
        return;
    m_sounds[row].durationMs = durationMs;
    emit dataChanged(index(row, DurationColumn), index(row, DurationColumn));
    scheduleSave();
}

void SoundLibrary::setPath(int id, const QString &path)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_sounds[row].path = QFileInfo(path).absoluteFilePath();
    m_sounds[row].durationMs = -1;
    if (QFileInfo::exists(path))
        m_missing.remove(id);
    else
        m_missing.insert(id);
    soundChanged(id);
    scheduleSave();
}

void SoundLibrary::recordPlay(int id)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    ++m_sounds[row].playCount;
    m_sounds[row].lastPlayedAt = QDateTime::currentDateTime();
    emit dataChanged(index(row, PlaysColumn), index(row, PlaysColumn));
    scheduleSave();
}

void SoundLibrary::moveSounds(const QList<int> &ids, int beforeId)
{
    if (ids.isEmpty() || ids.contains(beforeId))
        return;
    beginResetModel();
    QList<Sound> moving;
    for (const Sound &sound : std::as_const(m_sounds)) {
        if (ids.contains(sound.id))
            moving.append(sound);
    }
    m_sounds.removeIf([&ids](const Sound &s) { return ids.contains(s.id); });
    int insertAt = beforeId == 0 ? m_sounds.size() : rowOf(beforeId);
    if (insertAt < 0)
        insertAt = m_sounds.size();
    for (int i = 0; i < moving.size(); ++i)
        m_sounds.insert(insertAt + i, moving.at(i));
    endResetModel();
    scheduleSave();
}

int SoundLibrary::findByHotkey(const QString &portableHotkey, int excludeId) const
{
    if (portableHotkey.isEmpty())
        return 0;
    const QKeySequence wanted(portableHotkey, QKeySequence::PortableText);
    for (const Sound &sound : m_sounds) {
        if (sound.id != excludeId && !sound.hotkey.isEmpty()
            && QKeySequence(sound.hotkey, QKeySequence::PortableText) == wanted)
            return sound.id;
    }
    return 0;
}

void SoundLibrary::refreshMissing()
{
    QSet<int> missing;
    for (const Sound &sound : std::as_const(m_sounds)) {
        if (!QFileInfo::exists(sound.path))
            missing.insert(sound.id);
    }
    if (missing == m_missing)
        return;
    m_missing = missing;
    if (!m_sounds.isEmpty())
        emit dataChanged(index(0, 0), index(m_sounds.size() - 1, ColumnCount - 1));
}

void SoundLibrary::setPlaying(const QSet<int> &soundIds)
{
    if (soundIds == m_playing)
        return;
    const QSet<int> affected = soundIds + m_playing;
    m_playing = soundIds;
    for (int id : affected)
        soundChanged(id, {Qt::FontRole, Qt::DecorationRole});
}

void SoundLibrary::setHotkeyErrors(const QHash<int, QString> &errors)
{
    if (errors == m_hotkeyErrors)
        return;
    QSet<int> affected;
    for (auto it = errors.begin(); it != errors.end(); ++it)
        affected.insert(it.key());
    for (auto it = m_hotkeyErrors.begin(); it != m_hotkeyErrors.end(); ++it)
        affected.insert(it.key());
    m_hotkeyErrors = errors;
    for (int id : std::as_const(affected))
        soundChanged(id, {Qt::DecorationRole, Qt::ToolTipRole});
}

void SoundLibrary::setHotkeyTriggers(const QHash<int, QString> &triggers)
{
    m_hotkeyTriggers = triggers;
}

QString SoundLibrary::categoryName(int id) const
{
    if (id == 0)
        return QString(QLatin1String(""));
    for (const Category &category : m_categories) {
        if (category.id == id)
            return category.name;
    }
    return {};
}

int SoundLibrary::addCategory(const QString &name)
{
    Category category;
    category.id = m_nextCategoryId++;
    category.name = name;
    m_categories.append(category);
    scheduleSave();
    emit categoriesChanged();
    return category.id;
}

void SoundLibrary::renameCategory(int id, const QString &name)
{
    for (Category &category : m_categories) {
        if (category.id == id && !name.isEmpty()) {
            category.name = name;
            if (!m_sounds.isEmpty())
                emit dataChanged(index(0, CategoryColumn),
                                 index(m_sounds.size() - 1, CategoryColumn));
            scheduleSave();
            emit categoriesChanged();
            return;
        }
    }
}

void SoundLibrary::removeCategory(int id)
{
    const auto removed = m_categories.removeIf([id](const Category &c) { return c.id == id; });
    if (removed == 0)
        return;
    for (Sound &sound : m_sounds) {
        if (sound.categoryId == id)
            sound.categoryId = 0;
    }
    if (!m_sounds.isEmpty())
        emit dataChanged(index(0, 0), index(m_sounds.size() - 1, ColumnCount - 1));
    scheduleSave();
    emit categoriesChanged();
}

} // namespace sb
