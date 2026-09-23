#include "TrackTableModel.h"
#include "../ui/CoverThumb.h"
#include "../ui/TrackMime.h"
#include <QMimeData>
#include <QSet>
#include <algorithm>

TrackTableModel::TrackTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void TrackTableModel::setTracks(const QVector<Track> &tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();

    QSet<QString> live;
    for (const Track &track : m_tracks) {
        if (!track.coverPath.isEmpty()) live.insert(track.coverPath);
    }
    const QList<QString> keys = m_thumbs.keys();
    for (const QString &key : keys) {
        if (!live.contains(key)) m_thumbs.remove(key);
    }
}

void TrackTableModel::updateTrackCover(qint64 trackId, const QString &coverPath)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            const QString previous = m_tracks[i].coverPath;
            m_tracks[i].coverPath = coverPath;
            m_thumbs.remove(previous);
            m_thumbs.remove(coverPath);
            const QModelIndex idx = index(i, ColCover);
            emit dataChanged(idx, idx, {Qt::DecorationRole});
            break;
        }
    }
}

int TrackTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

int TrackTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TrackTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size()) return {};
    const Track &t = m_tracks[index.row()];

    if (role == Qt::DecorationRole && index.column() == ColCover)
        return coverThumb(t.coverPath);
    if (role == Qt::UserRole) return t.id;

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColDuration) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTitle: return t.title;
        case ColArtistAlbum: return t.album.isEmpty() ? t.artist : (t.artist + " — " + t.album);
        case ColDuration: {
            const qint64 totalSec = t.durationMs / 1000;
            return QStringLiteral("%1:%2").arg(totalSec / 60).arg(totalSec % 60, 2, 10, QChar('0'));
        }
        default: return {};
        }
    }
    return {};
}

Qt::ItemFlags TrackTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid()) f |= Qt::ItemIsDragEnabled;
    if (m_reorderable) f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList TrackTableModel::mimeTypes() const
{
    return {QString::fromLatin1(kTrackIdsMime)};
}

QMimeData *TrackTableModel::mimeData(const QModelIndexList &indexes) const
{
    QList<int> rows;
    for (const QModelIndex &idx : indexes) {
        if (idx.isValid() && !rows.contains(idx.row())) rows.push_back(idx.row());
    }
    std::sort(rows.begin(), rows.end());

    QList<qint64> ids;
    for (int row : rows) {
        if (row >= 0 && row < m_tracks.size()) ids.push_back(m_tracks[row].id);
    }

    auto *mime = new QMimeData;
    writeTrackIds(mime, ids);
    return mime;
}

bool TrackTableModel::reorderRows(int from, int to)
{
    if (!m_reorderable || from == to || from < 0 || to < 0
        || from >= m_tracks.size() || to >= m_tracks.size())
        return false;
    beginResetModel();
    m_tracks.move(from, to);
    endResetModel();
    emit orderChanged();
    return true;
}

QPixmap TrackTableModel::coverThumb(const QString &path) const
{
    if (path.isEmpty()) return {};
    if (const QPixmap *found = m_thumbs.object(path)) return *found;
    const QPixmap thumb = squareCoverThumb(path, 128);
    if (thumb.isNull()) return {};
    m_thumbs.insert(path, new QPixmap(thumb));
    return thumb;
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        if (section == ColDuration) return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColCover: return "";
    case ColTitle: return QObject::tr("Название");
    case ColArtistAlbum: return QObject::tr("Исполнитель — Альбом");
    case ColDuration: return QObject::tr("Длит.");
    default: return {};
    }
}
