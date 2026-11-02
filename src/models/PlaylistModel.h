#pragma once
#include <QAbstractListModel>
#include <QCache>
#include <QPixmap>
#include "../db/Database.h"

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        HeaderRole = Qt::UserRole + 1,
        CategoryRole = Qt::UserRole + 2
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    void setPlaylists(const QVector<Playlist> &playlists);
    qint64 idAt(int row) const;
    bool isHeader(int row) const;
    Playlist playlistAt(int row) const;

    // 0 playlist, 1 single, 2 mini-album, 3 album. Automatic rows are guessed
    // from the track count: 1 single, 2–6 mini-album, 7+ album, empty stays a playlist.
    static int effectiveCategory(const Playlist &playlist);
    static int categoryRank(int category);
    static QString categoryLabel(int category);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct Row {
        bool header = false;
        Playlist playlist;
        int category = 0;
    };

    QVector<Row> m_rows;
    mutable QCache<QString, QPixmap> m_thumbs{48};
};
