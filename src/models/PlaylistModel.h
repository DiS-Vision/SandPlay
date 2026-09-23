#pragma once
#include <QAbstractListModel>
#include <QCache>
#include <QPixmap>
#include "../db/Database.h"

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit PlaylistModel(QObject *parent = nullptr);

    void setPlaylists(const QVector<Playlist> &playlists);
    qint64 idAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QVector<Playlist> m_playlists;
    mutable QCache<QString, QPixmap> m_thumbs{48};
};
