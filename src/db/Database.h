#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QVector>
#include <memory>
#include <optional>

struct Track {
    qint64 id = -1;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString genre;
    qint64 durationMs = 0;
    QString format;          // "wav" | "flac" | "mp3"
    QString coverPath;       // local disk path once cached — never a remote URL
    QString coverSource;     // "embedded" | "manual" | "album" | "caa" | "itunes" | "unavailable" | ""
};

struct Playlist {
    qint64 id = -1;
    QString name;
    QString coverPath;   // manually-set cover, local disk path — empty if none chosen yet
};

struct WatchedFolder {
    QString path;
    bool recursive = false;
    qint64 playlistId = -1;
    bool importNew = false;
};

// Thin wrapper around a single QSqlDatabase connection (QSQLITE driver,
// bundled with Qt — no separate sqlite3 dependency to manage). All calls
// are expected from the same thread; AudioEngine and network code never
// touch this directly, they go through LibraryManager/CoverCache.
struct TrackSearchIndex;

class Database {
public:
    // dbFilePath: e.g. QStandardPaths::AppDataLocation + "/library.db"
    explicit Database(const QString &dbFilePath);
    ~Database();
    bool init();

    // --- Tracks ---
    std::optional<Track> insertTrack(const Track &t);   // ON CONFLICT(path) DO NOTHING
    QVector<Track> queryTracks(const QString &search, int limit, int offset) const;
    int countTracks(const QString &search) const;
    QVector<Track> allTracks() const;
    bool updateTrackPath(qint64 trackId, const QString &path);
    std::optional<Track> trackById(qint64 trackId) const;
    qint64 trackIdForPath(const QString &path) const;
    // Tracks that still have no image and have not been marked "unavailable".
    QVector<Track> tracksMissingCovers() const;
    // A local cover already stored for another track of the same release, if any.
    QString findCoverForAlbum(const QString &artist, const QString &album) const;
    // source is "embedded", "manual", "album" (copied from a sibling track),
    // "deezer" / "itunes" / "caa" (fetched and cached), or "unavailable".
    bool setTrackCover(qint64 trackId, const QString &coverPath, const QString &source);
    // Lets a new lookup pipeline retry tracks the previous one gave up on.
    bool forgetMissedCovers();
    bool updateTrackInfo(qint64 trackId, const QString &title, const QString &artist,
                         const QString &album, const QString &genre);
    // Drops the library row. Playlist membership goes with it.
    bool deleteTrack(qint64 trackId);
    bool coverStillUsed(const QString &coverPath, qint64 exceptTrackId) const;
    void clearPlaylistCovers(const QString &coverPath);

    // --- Playlists ---
    qint64 createPlaylist(const QString &name);
    QVector<Playlist> allPlaylists() const;
    bool renamePlaylist(qint64 playlistId, const QString &name);
    bool deletePlaylist(qint64 playlistId);
    static constexpr int kPlaylistTrackLimit = 2000;
    int playlistTrackCount(qint64 playlistId) const;
    bool addTrackToPlaylist(qint64 playlistId, qint64 trackId);
    bool removeTrackFromPlaylist(qint64 playlistId, qint64 trackId);
    bool setPlaylistTrackOrder(qint64 playlistId, const QVector<qint64> &trackIds);
    QVector<Track> playlistTracks(qint64 playlistId) const;
    bool setPlaylistCover(qint64 playlistId, const QString &coverPath);

    // Folders the library mirrors: new files are imported, missing files drop out,
    // a rename inside the folder keeps the same track row.
    bool upsertWatchedFolder(const QString &path, bool recursive, qint64 playlistId, bool importNew);
    QVector<WatchedFolder> watchedFolders() const;

    // --- Skins (WYSIWYG layout: per-element x/y plus color/font tokens) ---
    bool saveSkin(const QString &name, const QString &layoutJson);
    bool setActiveSkin(const QString &name);
    QString activeSkinLayoutJson() const;

    // --- Small key/value settings (queue mode, last playlist, etc.) ---
    QString getSetting(const QString &key, const QString &fallback = QString()) const;
    bool setSetting(const QString &key, const QString &value);

private:
    QString m_path;
    QString m_connectionName;
    mutable std::unique_ptr<TrackSearchIndex> m_search;
    mutable bool m_searchDirty = true;
    QSqlDatabase db() const;
    void ensureSearchIndex() const;
};
