#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

class QMimeData;
class QTimer;

namespace sb {

struct Sound {
    int id = 0;
    QString path;
    QString title;
    QString hotkey;        // QKeySequence::PortableText, empty when unassigned
    int categoryId = 0;    // 0 means "no category"
    int volume = 100;      // percent, 0..200
    qint64 durationMs = -1;
    int playCount = 0;
    QDateTime addedAt;
    QDateTime lastPlayedAt;
};

struct Category {
    int id = 0;
    QString name;
};

// The user's sound list and categories, persisted as JSON. It doubles as the
// table model shown in the main window.
class SoundLibrary : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        IndexColumn,
        TitleColumn,
        HotkeyColumn,
        DurationColumn,
        CategoryColumn,
        PlaysColumn,
        AddedColumn,
        PathColumn,
        ColumnCount
    };
    enum Role { IdRole = Qt::UserRole + 1, SortRole, CategoryIdRole, SearchRole };

    static const QString MimeType;

    explicit SoundLibrary(const QString &filePath, QObject *parent = nullptr);
    ~SoundLibrary() override;

    QString filePath() const { return m_filePath; }
    bool load(QString *error);
    bool save(QString *error = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;

    int count() const { return m_sounds.size(); }
    const Sound &at(int row) const { return m_sounds.at(row); }
    const Sound *find(int id) const;
    int rowOf(int id) const;
    const QList<Sound> &sounds() const { return m_sounds; }

    // Adds files that are not in the library yet; returns the new ids.
    QList<int> addFiles(const QStringList &paths, int categoryId, int *skipped = nullptr);
    void removeSounds(const QList<int> &ids);
    void setTitle(int id, const QString &title);
    void setHotkey(int id, const QString &portableHotkey);
    void setCategory(const QList<int> &ids, int categoryId);
    void setVolume(int id, int percent);
    void setDuration(int id, qint64 durationMs);
    void setPath(int id, const QString &path);
    void recordPlay(int id);
    // Moves the sounds so they sit right before `beforeId` (or at the end when 0).
    void moveSounds(const QList<int> &ids, int beforeId);
    int findByHotkey(const QString &portableHotkey, int excludeId = 0) const;

    bool isMissing(int id) const { return m_missing.contains(id); }
    void refreshMissing();
    void setPlaying(const QSet<int> &soundIds);
    void setHotkeyErrors(const QHash<int, QString> &errors);
    // Triggers the desktop actually assigned (Wayland portal), shown as a tooltip.
    void setHotkeyTriggers(const QHash<int, QString> &triggers);

    const QList<Category> &categories() const { return m_categories; }
    QString categoryName(int id) const;
    int addCategory(const QString &name);
    void renameCategory(int id, const QString &name);
    void removeCategory(int id);

    static QList<int> idsFromMimeData(const QMimeData *mime);
    static QString formatDuration(qint64 ms);

signals:
    void categoriesChanged();
    void hotkeysChanged();

private:
    void soundChanged(int id, const QList<int> &roles = {});
    void scheduleSave();

    QString m_filePath;
    QList<Sound> m_sounds;
    QList<Category> m_categories;
    int m_nextSoundId = 1;
    int m_nextCategoryId = 1;
    QSet<int> m_missing;
    QSet<int> m_playing;
    QHash<int, QString> m_hotkeyErrors;
    QHash<int, QString> m_hotkeyTriggers;
    QTimer *m_saveTimer = nullptr;
};

} // namespace sb
