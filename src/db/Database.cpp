#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <functional>

namespace {

struct TrieNode {
    QHash<QChar, TrieNode *> next;
    QVector<qint64> ids;
    ~TrieNode() { qDeleteAll(next); }
};

QString fold(QString text)
{
    text = text.toLower();
    text.replace(QChar(u'ё'), QChar(u'е'));
    return text;
}

void forEachWord(const QString &text, const std::function<void(const QString &)> &fn)
{
    QString word;
    for (const QChar ch : fold(text)) {
        if (ch.isLetterOrNumber()) {
            word.append(ch);
            continue;
        }
        if (!word.isEmpty()) {
            fn(word);
            word.clear();
        }
    }
    if (!word.isEmpty()) fn(word);
}

} // namespace

struct TrackSearchIndex {
    TrieNode root;
    QHash<qint64, Track> tracks;

    void addWord(const QString &word, qint64 id)
    {
        TrieNode *node = &root;
        for (const QChar ch : word) {
            auto it = node->next.find(ch);
            if (it == node->next.end())
                it = node->next.insert(ch, new TrieNode);
            node = it.value();
        }
        if (!node->ids.contains(id)) node->ids.push_back(id);
    }

    void add(const Track &track)
    {
        tracks.insert(track.id, track);
        // Index every suffix so a query matches inside a word ("time" finds
        // "summertime"), not only a word that starts with the query.
        const auto index = [this, id = track.id](const QString &word) {
            for (int i = 0; i < word.size(); ++i)
                addWord(word.mid(i), id);
        };
        forEachWord(track.title, index);
        forEachWord(track.artist, index);
        forEachWord(track.album, index);
    }

    int count(const QString &query) const
    {
        QStringList tokens;
        forEachWord(query, [&](const QString &word) { tokens.push_back(word); });
        if (tokens.isEmpty()) return 0;
        QSet<qint64> matched = idsWithPrefix(tokens.first());
        for (int i = 1; i < tokens.size(); ++i)
            matched.intersect(idsWithPrefix(tokens.at(i)));
        return matched.size();
    }

    void collect(const TrieNode *node, QSet<qint64> *out) const
    {
        for (qint64 id : node->ids) out->insert(id);
        for (const TrieNode *child : node->next) collect(child, out);
    }

    QSet<qint64> idsWithPrefix(const QString &token) const
    {
        const TrieNode *node = &root;
        for (const QChar ch : token) {
            const auto it = node->next.constFind(ch);
            if (it == node->next.cend()) return {};
            node = it.value();
        }
        QSet<qint64> out;
        collect(node, &out);
        return out;
    }

    QVector<Track> match(const QString &query, int limit, int offset) const
    {
        QStringList tokens;
        forEachWord(query, [&](const QString &word) { tokens.push_back(word); });
        if (tokens.isEmpty()) return {};

        QSet<qint64> matched = idsWithPrefix(tokens.first());
        for (int i = 1; i < tokens.size(); ++i)
            matched.intersect(idsWithPrefix(tokens.at(i)));

        QVector<Track> rows;
        rows.reserve(matched.size());
        for (qint64 id : matched) {
            const auto it = tracks.constFind(id);
            if (it != tracks.cend()) rows.push_back(it.value());
        }
        std::sort(rows.begin(), rows.end(), [](const Track &a, const Track &b) {
            const int byTitle = QString::compare(a.title, b.title, Qt::CaseInsensitive);
            if (byTitle != 0) return byTitle < 0;
            return QString::compare(a.artist, b.artist, Qt::CaseInsensitive) < 0;
        });
        if (offset > 0) rows = rows.mid(offset);
        if (limit >= 0 && rows.size() > limit) rows.resize(limit);
        return rows;
    }
};

Database::Database(const QString &dbFilePath)
    : m_path(dbFilePath)
    , m_connectionName(QStringLiteral("library-") + QUuid::createUuid().toString())
{
}

Database::~Database() = default;

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool Database::init()
{
    auto database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    database.setDatabaseName(m_path);
    if (!database.open()) {
        qWarning() << "failed to open library db:" << database.lastError().text();
        return false;
    }

    QSqlQuery q(database);
    // WAL avoids growing a single rollback journal and lets reads happen
    // while a write (e.g. cover-cache update) is in flight.
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("PRAGMA foreign_keys=ON");

    const char *schema = R"SQL(
        CREATE TABLE IF NOT EXISTS tracks (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            path         TEXT NOT NULL UNIQUE,
            title        TEXT,
            artist       TEXT,
            album        TEXT,
            genre        TEXT,
            duration_ms  INTEGER,
            format       TEXT,
            cover_path   TEXT,
            cover_source TEXT,
            added_at     INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS playlists (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            name       TEXT NOT NULL,
            cover_path TEXT,
            created_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS playlist_tracks (
            playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
            track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            position    INTEGER NOT NULL,
            PRIMARY KEY (playlist_id, track_id)
        );

        CREATE TABLE IF NOT EXISTS skins (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL UNIQUE,
            layout_json TEXT NOT NULL,   -- per-element {x,y,w,h} + color/font tokens
            is_active   INTEGER NOT NULL DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS settings (
            key   TEXT PRIMARY KEY,
            value TEXT
        );

        CREATE TABLE IF NOT EXISTS watched_folders (
            path        TEXT PRIMARY KEY,
            recursive   INTEGER NOT NULL DEFAULT 0,
            playlist_id INTEGER REFERENCES playlists(id) ON DELETE SET NULL,
            import_new  INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);
        CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album);
        CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
            title, artist, album, content='tracks', content_rowid='id'
        );
    )SQL";

    if (!q.exec(schema)) {
        // execute statements one-by-one — QSqlQuery::exec doesn't run multi-statement batches
        for (const QString &stmt : QString(schema).split(';', Qt::SkipEmptyParts)) {
            if (stmt.trimmed().isEmpty()) continue;
            if (!q.exec(stmt)) qWarning() << "schema statement failed:" << q.lastError().text();
        }
    }

    // FTS5 external-content tables stay empty unless these triggers run.
    // They are executed as whole statements — the schema splitter above
    // cuts on ';', which would break a trigger body.
    const char *triggers[] = {
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
            INSERT INTO tracks_fts(rowid, title, artist, album) VALUES (new.id, new.title, new.artist, new.album);
        END)SQL",
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album) VALUES('delete', old.id, old.title, old.artist, old.album);
        END)SQL",
        R"SQL(CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album) VALUES('delete', old.id, old.title, old.artist, old.album);
            INSERT INTO tracks_fts(rowid, title, artist, album) VALUES (new.id, new.title, new.artist, new.album);
        END)SQL",
    };
    for (const char *sql : triggers) {
        if (!q.exec(QString::fromUtf8(sql)))
            qWarning() << "fts trigger failed:" << q.lastError().text();
    }
    if (!q.exec("INSERT INTO tracks_fts(tracks_fts) VALUES('rebuild')"))
        qWarning() << "fts rebuild failed:" << q.lastError().text();

    return true;
}

namespace {

Track trackFromQuery(const QSqlQuery &q)
{
    Track t;
    t.id = q.value(0).toLongLong();
    t.path = q.value(1).toString();
    t.title = q.value(2).toString();
    t.artist = q.value(3).toString();
    t.album = q.value(4).toString();
    t.genre = q.value(5).toString();
    t.durationMs = q.value(6).toLongLong();
    t.format = q.value(7).toString();
    t.coverPath = q.value(8).toString();
    t.coverSource = q.value(9).toString();
    return t;
}

} // namespace

std::optional<Track> Database::insertTrack(const Track &t)
{
    QSqlQuery q(db());
    q.prepare(R"(
        INSERT INTO tracks (path, title, artist, album, genre, duration_ms, format, cover_path, cover_source, added_at)
        VALUES (:path, :title, :artist, :album, :genre, :duration, :format, :cover, :source, :added)
        ON CONFLICT(path) DO NOTHING
    )");
    q.bindValue(":path", t.path);
    q.bindValue(":title", t.title);
    q.bindValue(":artist", t.artist);
    q.bindValue(":album", t.album);
    q.bindValue(":genre", t.genre);
    q.bindValue(":duration", t.durationMs);
    q.bindValue(":format", t.format);
    q.bindValue(":cover", t.coverPath);
    q.bindValue(":source", t.coverSource);
    q.bindValue(":added", QDateTime::currentSecsSinceEpoch());

    if (!q.exec()) {
        qWarning() << "insertTrack failed:" << q.lastError().text();
        return std::nullopt;
    }
    m_searchDirty = true;

    Track result = t;
    result.id = q.lastInsertId().isValid() ? q.lastInsertId().toLongLong()
                                            : -1; // already existed (ON CONFLICT DO NOTHING)
    return result;
}

qint64 Database::trackIdForPath(const QString &path) const
{
    QSqlQuery q(db());
    q.prepare("SELECT id FROM tracks WHERE path = :path");
    q.bindValue(":path", path);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toLongLong();
}

QVector<Track> Database::queryTracks(const QString &search, int limit, int offset) const
{
    QSqlQuery q(db());
    if (search.trimmed().isEmpty()) {
        q.prepare(R"(
            SELECT id, path, title, artist, album, genre, duration_ms, format, cover_path, cover_source
            FROM tracks ORDER BY added_at DESC LIMIT :limit OFFSET :offset
        )");
    } else {
        ensureSearchIndex();
        if (!m_search) return {};
        return m_search->match(search, limit, offset);
    }
    q.bindValue(":limit", limit);
    q.bindValue(":offset", offset);

    QVector<Track> out;
    if (!q.exec()) {
        qWarning() << "queryTracks failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(trackFromQuery(q));
    return out;
}

int Database::countTracks(const QString &search) const
{
    if (search.trimmed().isEmpty()) {
        QSqlQuery q(db());
        if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks")) || !q.next()) return 0;
        return q.value(0).toInt();
    }
    ensureSearchIndex();
    if (!m_search) return 0;
    return m_search->count(search);
}

QVector<Track> Database::allTracks() const
{
    return queryTracks(QString(), -1, 0);
}

bool Database::updateTrackPath(qint64 trackId, const QString &path)
{
    QSqlQuery q(db());
    q.prepare("UPDATE tracks SET path = :path WHERE id = :id");
    q.bindValue(":path", path);
    q.bindValue(":id", trackId);
    if (!q.exec()) return false;
    m_searchDirty = true;
    return q.numRowsAffected() > 0;
}

void Database::ensureSearchIndex() const
{
    if (!m_searchDirty && m_search) return;
    auto index = std::make_unique<TrackSearchIndex>();
    QSqlQuery q(db());
    if (q.exec(QStringLiteral(
            "SELECT id, path, title, artist, album, genre, duration_ms, format, cover_path, cover_source FROM tracks"))) {
        while (q.next()) index->add(trackFromQuery(q));
    } else {
        qWarning() << "search index failed:" << q.lastError().text();
    }
    m_search = std::move(index);
    m_searchDirty = false;
}

std::optional<Track> Database::trackById(qint64 trackId) const
{
    QSqlQuery q(db());
    q.prepare(R"(
        SELECT id, path, title, artist, album, genre, duration_ms, format, cover_path, cover_source
        FROM tracks WHERE id = :id
    )");
    q.bindValue(":id", trackId);
    if (!q.exec() || !q.next()) return std::nullopt;
    return trackFromQuery(q);
}

QVector<Track> Database::tracksMissingCovers() const
{
    QSqlQuery q(R"(
        SELECT id, path, title, artist, album, genre, duration_ms, format, cover_path, cover_source
        FROM tracks
        WHERE (cover_path IS NULL OR cover_path = '')
          AND IFNULL(cover_source, '') != 'unavailable'
        ORDER BY artist, album, title
    )", db());
    QVector<Track> out;
    while (q.next()) out.push_back(trackFromQuery(q));
    return out;
}

QString Database::findCoverForAlbum(const QString &artist, const QString &album) const
{
    if (artist.trimmed().isEmpty() || album.trimmed().isEmpty()) return {};
    QSqlQuery q(db());
    q.prepare(R"(
        SELECT cover_path FROM tracks
        WHERE artist = :artist AND album = :album
          AND cover_path IS NOT NULL AND cover_path != ''
        LIMIT 1
    )");
    q.bindValue(":artist", artist.trimmed());
    q.bindValue(":album", album.trimmed());
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

bool Database::setTrackCover(qint64 trackId, const QString &coverPath, const QString &source)
{
    QSqlQuery q(db());
    q.prepare("UPDATE tracks SET cover_path = :cover, cover_source = :source WHERE id = :id");
    q.bindValue(":cover", coverPath);
    q.bindValue(":source", source);
    q.bindValue(":id", trackId);
    if (!q.exec()) return false;
    m_searchDirty = true;
    return true;
}

bool Database::forgetMissedCovers()
{
    QSqlQuery q(db());
    return q.exec("UPDATE tracks SET cover_source = '' WHERE cover_source = 'unavailable'");
}

bool Database::updateTrackInfo(qint64 trackId, const QString &title, const QString &artist,
                               const QString &album, const QString &genre)
{
    QSqlQuery q(db());
    q.prepare("UPDATE tracks SET title = :title, artist = :artist, album = :album, genre = :genre WHERE id = :id");
    q.bindValue(":title", title);
    q.bindValue(":artist", artist);
    q.bindValue(":album", album);
    q.bindValue(":genre", genre);
    q.bindValue(":id", trackId);
    if (!q.exec()) return false;
    m_searchDirty = true;
    return true;
}

bool Database::deleteTrack(qint64 trackId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM tracks WHERE id = :id");
    q.bindValue(":id", trackId);
    if (!q.exec()) return false;
    m_searchDirty = true;
    return q.numRowsAffected() > 0;
}

bool Database::coverStillUsed(const QString &coverPath, qint64 exceptTrackId) const
{
    if (coverPath.isEmpty()) return false;
    QSqlQuery tracks(db());
    tracks.prepare("SELECT COUNT(*) FROM tracks WHERE cover_path = :path AND id != :id");
    tracks.bindValue(":path", coverPath);
    tracks.bindValue(":id", exceptTrackId);
    if (tracks.exec() && tracks.next() && tracks.value(0).toInt() > 0) return true;
    QSqlQuery playlists(db());
    playlists.prepare("SELECT COUNT(*) FROM playlists WHERE cover_path = :path");
    playlists.bindValue(":path", coverPath);
    return playlists.exec() && playlists.next() && playlists.value(0).toInt() > 0;
}

void Database::clearPlaylistCovers(const QString &coverPath)
{
    if (coverPath.isEmpty()) return;
    QSqlQuery q(db());
    q.prepare("UPDATE playlists SET cover_path = '' WHERE cover_path = :path");
    q.bindValue(":path", coverPath);
    q.exec();
}

qint64 Database::createPlaylist(const QString &name)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO playlists (name, created_at) VALUES (:name, :now)");
    q.bindValue(":name", name);
    q.bindValue(":now", QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) { qWarning() << q.lastError().text(); return -1; }
    return q.lastInsertId().toLongLong();
}

bool Database::renamePlaylist(qint64 playlistId, const QString &name)
{
    QSqlQuery q(db());
    q.prepare("UPDATE playlists SET name = :name WHERE id = :id");
    q.bindValue(":name", name);
    q.bindValue(":id", playlistId);
    return q.exec();
}

bool Database::deletePlaylist(qint64 playlistId)
{
    auto database = db();
    if (!database.transaction()) return false;
    QSqlQuery tracks(database);
    tracks.prepare("DELETE FROM playlist_tracks WHERE playlist_id = :id");
    tracks.bindValue(":id", playlistId);
    QSqlQuery list(database);
    list.prepare("DELETE FROM playlists WHERE id = :id");
    list.bindValue(":id", playlistId);
    if (!tracks.exec() || !list.exec()) {
        database.rollback();
        return false;
    }
    return database.commit();
}

QVector<Playlist> Database::allPlaylists() const
{
    QSqlQuery q("SELECT id, name, cover_path FROM playlists ORDER BY created_at", db());
    QVector<Playlist> out;
    while (q.next()) out.push_back({ q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString() });
    return out;
}

bool Database::setPlaylistCover(qint64 playlistId, const QString &coverPath)
{
    QSqlQuery q(db());
    q.prepare("UPDATE playlists SET cover_path = :cover WHERE id = :id");
    q.bindValue(":cover", coverPath);
    q.bindValue(":id", playlistId);
    return q.exec();
}

int Database::playlistTrackCount(qint64 playlistId) const
{
    QSqlQuery q(db());
    q.prepare("SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = :pl");
    q.bindValue(":pl", playlistId);
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

bool Database::addTrackToPlaylist(qint64 playlistId, qint64 trackId)
{
    if (playlistTrackCount(playlistId) >= kPlaylistTrackLimit) return false;

    QSqlQuery pos(db());
    pos.prepare("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_tracks WHERE playlist_id = :pl");
    pos.bindValue(":pl", playlistId);
    qint64 nextPos = 0;
    if (pos.exec() && pos.next()) nextPos = pos.value(0).toLongLong();

    QSqlQuery q(db());
    q.prepare(R"(INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position)
                 VALUES (:pl, :tr, :pos))");
    q.bindValue(":pl", playlistId);
    q.bindValue(":tr", trackId);
    q.bindValue(":pos", nextPos);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::removeTrackFromPlaylist(qint64 playlistId, qint64 trackId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM playlist_tracks WHERE playlist_id = :pl AND track_id = :tr");
    q.bindValue(":pl", playlistId);
    q.bindValue(":tr", trackId);
    return q.exec();
}

bool Database::setPlaylistTrackOrder(qint64 playlistId, const QVector<qint64> &trackIds)
{
    auto database = db();
    if (!database.transaction()) return false;
    QSqlQuery del(database);
    del.prepare("DELETE FROM playlist_tracks WHERE playlist_id = :pl");
    del.bindValue(":pl", playlistId);
    if (!del.exec()) {
        database.rollback();
        return false;
    }
    QSqlQuery ins(database);
    ins.prepare(R"(INSERT INTO playlist_tracks (playlist_id, track_id, position)
                  VALUES (:pl, :tr, :pos))");
    for (int i = 0; i < trackIds.size(); ++i) {
        ins.bindValue(":pl", playlistId);
        ins.bindValue(":tr", trackIds[i]);
        ins.bindValue(":pos", i);
        if (!ins.exec()) {
            database.rollback();
            return false;
        }
    }
    return database.commit();
}

QVector<Track> Database::playlistTracks(qint64 playlistId) const
{
    QSqlQuery q(db());
    q.prepare(R"(
        SELECT t.id, t.path, t.title, t.artist, t.album, t.genre, t.duration_ms, t.format, t.cover_path, t.cover_source
        FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id
        WHERE pt.playlist_id = :pl ORDER BY pt.position
    )");
    q.bindValue(":pl", playlistId);

    QVector<Track> out;
    if (!q.exec()) return out;
    while (q.next()) out.push_back(trackFromQuery(q));
    return out;
}

bool Database::saveSkin(const QString &name, const QString &layoutJson)
{
    QSqlQuery q(db());
    q.prepare(R"(
        INSERT INTO skins (name, layout_json, is_active) VALUES (:name, :layout, 0)
        ON CONFLICT(name) DO UPDATE SET layout_json = excluded.layout_json
    )");
    q.bindValue(":name", name);
    q.bindValue(":layout", layoutJson);
    return q.exec();
}

bool Database::setActiveSkin(const QString &name)
{
    QSqlQuery(db()).exec("UPDATE skins SET is_active = 0");
    QSqlQuery q(db());
    q.prepare("UPDATE skins SET is_active = 1 WHERE name = :name");
    q.bindValue(":name", name);
    return q.exec();
}

QString Database::activeSkinLayoutJson() const
{
    QSqlQuery q("SELECT layout_json FROM skins WHERE is_active = 1 LIMIT 1", db());
    if (q.next()) return q.value(0).toString();
    return QString();
}

QString Database::getSetting(const QString &key, const QString &fallback) const
{
    QSqlQuery q(db());
    q.prepare("SELECT value FROM settings WHERE key = :k");
    q.bindValue(":k", key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return fallback;
}

bool Database::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO settings (key, value) VALUES (:k, :v) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.bindValue(":k", key);
    q.bindValue(":v", value);
    return q.exec();
}

bool Database::upsertWatchedFolder(const QString &path, bool recursive, qint64 playlistId, bool importNew)
{
    QSqlQuery q(db());
    q.prepare(R"(
        INSERT INTO watched_folders (path, recursive, playlist_id, import_new)
        VALUES (:path, :recursive, :playlist, :importNew)
        ON CONFLICT(path) DO UPDATE SET
            recursive = excluded.recursive,
            import_new = CASE
                WHEN excluded.import_new != 0 THEN 1
                ELSE watched_folders.import_new
            END,
            playlist_id = CASE
                WHEN excluded.playlist_id IS NOT NULL THEN excluded.playlist_id
                ELSE watched_folders.playlist_id
            END
    )");
    q.bindValue(":path", path);
    q.bindValue(":recursive", recursive ? 1 : 0);
    q.bindValue(":importNew", importNew ? 1 : 0);
    if (playlistId >= 0) q.bindValue(":playlist", playlistId);
    else q.bindValue(":playlist", QVariant());
    if (!q.exec()) {
        qWarning() << "upsertWatchedFolder failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QVector<WatchedFolder> Database::watchedFolders() const
{
    QSqlQuery q(db());
    QVector<WatchedFolder> out;
    if (!q.exec(QStringLiteral("SELECT path, recursive, playlist_id, import_new FROM watched_folders ORDER BY path")))
        return out;
    while (q.next()) {
        WatchedFolder folder;
        folder.path = q.value(0).toString();
        folder.recursive = q.value(1).toInt() != 0;
        folder.playlistId = q.value(2).isNull() ? -1 : q.value(2).toLongLong();
        folder.importNew = q.value(3).toInt() != 0;
        out.push_back(folder);
    }
    return out;
}
