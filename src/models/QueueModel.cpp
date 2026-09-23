#include "QueueModel.h"
#include "../ui/CoverThumb.h"
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QSet>

QueueModel::QueueModel(QObject *parent) : QAbstractListModel(parent) {}

void QueueModel::setQueue(const QVector<Track> &tracks, int currentIndex)
{
    beginResetModel();
    m_tracks = tracks;
    m_current = currentIndex;
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

int QueueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

Qt::ItemFlags QueueModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QVariant QueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size()) return {};
    const Track &track = m_tracks[index.row()];
    const bool current = index.row() == m_current;

    if (role == Qt::DisplayRole) {
        if (track.artist.isEmpty()) return track.title;
        return track.title + QStringLiteral("  —  ") + track.artist;
    }
    if (role == Qt::DecorationRole) {
        if (!track.coverPath.isEmpty()) {
            if (const QPixmap *found = m_thumbs.object(track.coverPath)) return QIcon(*found);
            const QPixmap art = squareCoverThumb(track.coverPath, 96);
            if (!art.isNull()) {
                m_thumbs.insert(track.coverPath, new QPixmap(art));
                return QIcon(art);
            }
        }
        static QIcon missing;
        if (missing.isNull()) {
            QPixmap disc(64, 64);
            disc.fill(Qt::transparent);
            QPainter painter(&disc);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#3a3f4d"));
            painter.drawEllipse(2, 2, 60, 60);
            painter.setBrush(QColor("#14161c"));
            painter.drawEllipse(18, 18, 28, 28);
            painter.setBrush(QColor("#8b8a86"));
            painter.drawEllipse(29, 29, 6, 6);
            missing = QIcon(disc);
        }
        return missing;
    }
    if (role == Qt::BackgroundRole && current)
        return QColor("#3a3428");
    if (role == Qt::ForegroundRole)
        return QColor(current ? "#f3efe6" : "#ece8e0");
    if (role == Qt::FontRole && current) {
        QFont font;
        font.setBold(true);
        return font;
    }
    return {};
}
