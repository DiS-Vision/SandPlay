#pragma once
#include <QString>
#include <QByteArray>

// All cover art — embedded-tag art or a manually chosen image — ends up as
// a JPEG on disk under <appdata>/covers/. The DB only ever stores that
// local path, so the player works fully offline and no cover is ever
// re-read from its original location after the first import.
//
// Tracks and playlists share this cache but use different filename
// prefixes so a track id and a playlist id never collide.
class CoverCache {
public:
    explicit CoverCache(const QString &cacheDir);

    QString pathForTrack(qint64 trackId) const;
    QString pathForPlaylist(qint64 playlistId) const;

    // Writes raw image bytes (e.g. an embedded tag picture) to disk,
    // re-encoding through QImage so format/size stay predictable.
    // Returns the local path on success, empty string on failure.
    QString storeTrackCoverBytes(qint64 trackId, const QByteArray &imageBytes);

    // Requirement: let the user pick an image file themselves when a track
    // or playlist has no cover (this replaces the old Last.fm fallback).
    QString storeTrackCoverFromFile(qint64 trackId, const QString &sourceImagePath);
    QString storePlaylistCoverFromFile(qint64 playlistId, const QString &sourceImagePath);

private:
    QString m_dir;
    // Re-encodes to JPEG capped at 600x600 (plenty for UI thumbnails; keeps
    // cache size and per-image memory use small) and writes to targetPath.
    QString reencodeAndStore(const QString &targetPath, const QByteArray &imageBytes) const;
};
