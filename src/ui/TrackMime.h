#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QMimeData>
#include <QString>

// Drag payload for library tracks. Used when dropping a track onto a
// playlist or into the playback queue.
inline constexpr char kTrackIdsMime[] = "application/x-sandplay-track-ids";

inline void writeTrackIds(QMimeData *mime, const QList<qint64> &ids)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << ids;
    mime->setData(QString::fromLatin1(kTrackIdsMime), payload);
}

inline QList<qint64> decodeTrackIds(const QMimeData *mime)
{
    QList<qint64> ids;
    if (!mime || !mime->hasFormat(QString::fromLatin1(kTrackIdsMime))) return ids;
    QByteArray payload = mime->data(QString::fromLatin1(kTrackIdsMime));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream >> ids;
    return ids;
}
