#pragma once
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include "../db/Database.h"
#include "CoverCache.h"

// Reads tags (title/artist/album/genre/duration + embedded picture) via
// TagLib and writes the result into the library database. Embedded covers
// are handed to CoverCache immediately, so `Track::coverPath` in the DB is
// always either empty or a local file path — never raw bytes, never a URL.
class LibraryManager : public QObject {
    Q_OBJECT
public:
    LibraryManager(Database &db, CoverCache &covers, QObject *parent = nullptr);

    static bool isSupportedFile(const QString &path); // extension check: wav/flac/mp3

    // Requirement #4 (original): single file via a native file picker.
    std::optional<Track> addTrack(const QString &path);
    // Writes title, artist, album and genre back into the audio file.
    bool writeTrackTags(const Track &track);

    // Requirement #6: import every supported file under a folder in one go.
    // Non-recursive by default (an "album" folder); pass recursive=true for
    // "playlist" folders that contain nested subfolders.
    // If createPlaylistNamedAfterFolder is true, a playlist is created from
    // the folder's name and every imported track is added to it.
    QVector<Track> importFolder(const QString &folderPath, bool recursive, bool createPlaylistNamedAfterFolder);

    // Remember imported folders (and, once, the folders already in the library)
    // and keep the rows in step with the disk.
    void startWatching();

signals:
    void importProgress(int done, int total);
    void trackImportFailed(const QString &path, const QString &reason);
    void libraryFoldersChanged(int added, const QList<qint64> &removedIds,
                               const QList<qint64> &renamedIds, const QStringList &renamedPaths);

private:
    Database &m_db;
    CoverCache &m_covers;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    QSet<QString> m_dirty;
    bool m_fullRescan = false;
    bool m_scanning = false;

    std::optional<Track> readTags(const QString &path);
    std::optional<Track> storeNewTrack(Track track);
    std::optional<Track> readAndInsert(const QString &path);
    void watchFolder(const QString &folderPath, bool recursive, qint64 playlistId, bool importNew);
    void onDirectoryChanged(const QString &path);
    void rescanDirty();
};
