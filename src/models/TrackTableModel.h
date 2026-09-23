#pragma once
#include <QAbstractTableModel>
#include <QCache>
#include <QPixmap>
#include "../db/Database.h"

class QMimeData;

class TrackTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColCover = 0, ColTitle, ColArtistAlbum, ColDuration, ColCount };

    explicit TrackTableModel(QObject *parent = nullptr);

    void setTracks(const QVector<Track> &tracks);
    const Track& trackAt(int row) const { return m_tracks[row]; }
    void updateTrackCover(qint64 trackId, const QString &coverPath);
    // Playlist view: drag a row onto another row to change its position.
    void setReorderable(bool reorderable) { m_reorderable = reorderable; }
    bool reorderRows(int from, int to);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction | Qt::MoveAction; }

signals:
    void orderChanged();

private:
    QPixmap coverThumb(const QString &path) const;

    QVector<Track> m_tracks;
    bool m_reorderable = false;
    mutable QCache<QString, QPixmap> m_thumbs{96};
};
