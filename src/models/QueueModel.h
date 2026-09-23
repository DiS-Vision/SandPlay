#pragma once

#include <QAbstractListModel>
#include <QCache>
#include <QPixmap>
#include "../db/Database.h"

class QueueModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit QueueModel(QObject *parent = nullptr);

    void setQueue(const QVector<Track> &tracks, int currentIndex);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QVector<Track> m_tracks;
    int m_current = -1;
    mutable QCache<QString, QPixmap> m_thumbs{64};
};
