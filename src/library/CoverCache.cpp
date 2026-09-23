#include "CoverCache.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QBuffer>

CoverCache::CoverCache(const QString &cacheDir) : m_dir(cacheDir)
{
    QDir().mkpath(m_dir);
}

QString CoverCache::pathForTrack(qint64 trackId) const
{
    return QDir(m_dir).filePath(QStringLiteral("track_%1.jpg").arg(trackId));
}

QString CoverCache::pathForPlaylist(qint64 playlistId) const
{
    return QDir(m_dir).filePath(QStringLiteral("playlist_%1.jpg").arg(playlistId));
}

QString CoverCache::reencodeAndStore(const QString &targetPath, const QByteArray &imageBytes) const
{
    QImage img;
    if (!img.loadFromData(imageBytes)) return {};

    // Cap thumbnail size — a personal library's covers otherwise add up
    // fast if kept at their original (sometimes multi-MB) resolution.
    if (img.width() > 600 || img.height() > 600) {
        img = img.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (!img.save(targetPath, "JPG", 85)) return {};
    return targetPath;
}

QString CoverCache::storeTrackCoverBytes(qint64 trackId, const QByteArray &imageBytes)
{
    if (imageBytes.isEmpty()) return {};
    return reencodeAndStore(pathForTrack(trackId), imageBytes);
}

QString CoverCache::storeTrackCoverFromFile(qint64 trackId, const QString &sourceImagePath)
{
    QFile f(sourceImagePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return reencodeAndStore(pathForTrack(trackId), f.readAll());
}

QString CoverCache::storePlaylistCoverFromFile(qint64 playlistId, const QString &sourceImagePath)
{
    QFile f(sourceImagePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return reencodeAndStore(pathForPlaylist(playlistId), f.readAll());
}
