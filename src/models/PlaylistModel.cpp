#include "PlaylistModel.h"
#include "../ui/CoverThumb.h"
#include <QIcon>
#include <QSet>

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

void PlaylistModel::setPlaylists(const QVector<Playlist> &playlists)
{
    beginResetModel();
    m_playlists = playlists;
    endResetModel();

    QSet<QString> live;
    for (const Playlist &playlist : m_playlists) {
        if (!playlist.coverPath.isEmpty()) live.insert(playlist.coverPath);
    }
    const QList<QString> keys = m_thumbs.keys();
    for (const QString &key : keys) {
        if (!live.contains(key)) m_thumbs.remove(key);
    }
}

qint64 PlaylistModel::idAt(int row) const
{
    return (row >= 0 && row < m_playlists.size()) ? m_playlists[row].id : -1;
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_playlists.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_playlists.size()) return {};
    const Playlist &p = m_playlists[index.row()];
    if (role == Qt::DisplayRole) return p.name;
    if (role == Qt::DecorationRole && !p.coverPath.isEmpty()) {
        if (const QPixmap *found = m_thumbs.object(p.coverPath)) return QIcon(*found);
        const QPixmap thumb = squareCoverThumb(p.coverPath, 64);
        if (thumb.isNull()) return {};
        m_thumbs.insert(p.coverPath, new QPixmap(thumb));
        return QIcon(thumb);
    }
    return {};
}
