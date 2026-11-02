#include "PlaylistModel.h"
#include "../ui/CoverThumb.h"
#include <QIcon>
#include <QSet>
#include <algorithm>

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

int PlaylistModel::effectiveCategory(const Playlist &playlist)
{
    if (playlist.category >= 0 && playlist.category <= 3)
        return playlist.category;
    if (playlist.trackCount <= 0) return 0;
    if (playlist.trackCount == 1) return 1;
    if (playlist.trackCount <= 6) return 2;
    return 3;
}

int PlaylistModel::categoryRank(int category)
{
    switch (category) {
    case 3: return 0;
    case 2: return 1;
    case 1: return 2;
    case 0: return 3;
    default: return 4;
    }
}

QString PlaylistModel::categoryLabel(int category)
{
    switch (category) {
    case 3: return tr("Альбом");
    case 2: return tr("Мини-альбом");
    case 1: return tr("Сингл");
    default: return tr("Плейлист");
    }
}

void PlaylistModel::setPlaylists(const QVector<Playlist> &playlists)
{
    QVector<Playlist> sorted = playlists;
    std::sort(sorted.begin(), sorted.end(), [](const Playlist &a, const Playlist &b) {
        const int ca = effectiveCategory(a);
        const int cb = effectiveCategory(b);
        if (ca != cb) return categoryRank(ca) < categoryRank(cb);
        auto folded = [](QString text) {
            text = text.toLower();
            text.replace(QChar(u'ё'), QChar(u'е'));
            return text;
        };
        const int name = folded(a.name).localeAwareCompare(folded(b.name));
        if (name != 0) return name < 0;
        return a.id < b.id;
    });

    beginResetModel();
    m_rows.clear();
    int previous = -1;
    for (const Playlist &playlist : sorted) {
        const int category = effectiveCategory(playlist);
        if (category != previous) {
            Row header;
            header.header = true;
            header.category = category;
            m_rows.push_back(header);
            previous = category;
        }
        Row row;
        row.playlist = playlist;
        row.category = category;
        m_rows.push_back(row);
    }
    endResetModel();

    QSet<QString> live;
    for (const Row &row : m_rows) {
        if (!row.header && !row.playlist.coverPath.isEmpty())
            live.insert(row.playlist.coverPath);
    }
    const QList<QString> keys = m_thumbs.keys();
    for (const QString &key : keys) {
        if (!live.contains(key)) m_thumbs.remove(key);
    }
}

qint64 PlaylistModel::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].header) return -1;
    return m_rows[row].playlist.id;
}

bool PlaylistModel::isHeader(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].header;
}

Playlist PlaylistModel::playlistAt(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].header) return {};
    return m_rows[row].playlist;
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows[index.row()];
    if (role == HeaderRole) return row.header;
    if (role == CategoryRole) return row.category;
    if (row.header) {
        if (role == Qt::DisplayRole) return categoryLabel(row.category);
        return {};
    }
    const Playlist &playlist = row.playlist;
    if (role == Qt::DisplayRole) return playlist.name;
    if (role == Qt::DecorationRole && !playlist.coverPath.isEmpty()) {
        if (const QPixmap *found = m_thumbs.object(playlist.coverPath)) return QIcon(*found);
        const QPixmap thumb = squareCoverThumb(playlist.coverPath, 64);
        if (thumb.isNull()) return {};
        m_thumbs.insert(playlist.coverPath, new QPixmap(thumb));
        return QIcon(thumb);
    }
    return {};
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return Qt::NoItemFlags;
    if (m_rows[index.row()].header) return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
