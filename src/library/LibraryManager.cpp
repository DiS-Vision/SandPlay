#include "LibraryManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QHash>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/wavfile.h>

namespace {

QByteArray extractEmbeddedPicture(const QString &path, const QString &format)
{
    const auto *wide = reinterpret_cast<const wchar_t *>(path.utf16());

    if (format == "mp3") {
        TagLib::MPEG::File f(wide);
        if (!f.isValid() || !f.ID3v2Tag()) return {};
        const auto frames = f.ID3v2Tag()->frameListMap()["APIC"];
        if (frames.isEmpty()) return {};
        auto *pic = static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
        return QByteArray(pic->picture().data(), pic->picture().size());
    }

    if (format == "flac") {
        TagLib::FLAC::File f(wide);
        if (!f.isValid()) return {};
        const auto pics = f.pictureList();
        if (pics.isEmpty()) return {};
        const auto &data = pics.front()->data();
        return QByteArray(data.data(), data.size());
    }

    if (format == "wav") {
        TagLib::RIFF::WAV::File f(wide);
        if (!f.isValid() || !f.hasID3v2Tag()) return {};
        const auto frames = f.ID3v2Tag()->frameListMap()["APIC"];
        if (frames.isEmpty()) return {};
        auto *pic = static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
        return QByteArray(pic->picture().data(), pic->picture().size());
    }

    return {};
}

QString canonicalPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString pathKey(const QString &path)
{
    return canonicalPath(path).toLower();
}

bool pathInFolder(const QString &filePath, const QString &folderPath, bool recursive)
{
    const QString file = pathKey(filePath);
    QString folder = pathKey(folderPath);
    if (!folder.endsWith(QLatin1Char('/'))) folder += QLatin1Char('/');
    if (!file.startsWith(folder)) return false;
    if (recursive) return true;
    return !file.mid(folder.size()).contains(QLatin1Char('/'));
}

QString renameSignature(const QString &title, const QString &artist, qint64 durationMs)
{
    QString foldedTitle = title.toLower();
    QString foldedArtist = artist.toLower();
    foldedTitle.replace(QChar(u'ё'), QChar(u'е'));
    foldedArtist.replace(QChar(u'ё'), QChar(u'е'));
    return foldedTitle + QLatin1Char('\n') + foldedArtist + QLatin1Char('\n')
        + QString::number(durationMs / 1000);
}

} // namespace

LibraryManager::LibraryManager(Database &db, CoverCache &covers, QObject *parent)
    : QObject(parent), m_db(db), m_covers(covers)
{
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(600);
    connect(&m_rescanTimer, &QTimer::timeout, this, &LibraryManager::rescanDirty);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &LibraryManager::onDirectoryChanged);
}

bool LibraryManager::isSupportedFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "wav" || ext == "flac" || ext == "fla" || ext == "mp3";
}

std::optional<Track> LibraryManager::readTags(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString format = suffix == QLatin1String("fla") ? QStringLiteral("flac") : suffix;
    const auto *wide = reinterpret_cast<const wchar_t *>(path.utf16());

    TagLib::FileRef ref;
    if (suffix == QLatin1String("fla")) {
        auto *flac = new TagLib::FLAC::File(wide);
        if (!flac->isValid()) {
            delete flac;
            emit trackImportFailed(path, "unreadable or unsupported file");
            return std::nullopt;
        }
        ref = TagLib::FileRef(flac);
    } else {
        ref = TagLib::FileRef(wide);
    }
    if (ref.isNull() || !ref.tag()) {
        emit trackImportFailed(path, "unreadable or unsupported file");
        return std::nullopt;
    }

    Track track;
    track.path = canonicalPath(path);
    track.format = format;
    track.title = QString::fromStdString(ref.tag()->title().to8Bit(true));
    track.artist = QString::fromStdString(ref.tag()->artist().to8Bit(true));
    track.album = QString::fromStdString(ref.tag()->album().to8Bit(true));
    track.genre = QString::fromStdString(ref.tag()->genre().to8Bit(true));
    if (ref.audioProperties()) track.durationMs = ref.audioProperties()->lengthInMilliseconds();
    if (track.title.isEmpty()) track.title = QFileInfo(path).completeBaseName();
    return track;
}

std::optional<Track> LibraryManager::storeNewTrack(Track track)
{
    auto inserted = m_db.insertTrack(track);
    if (!inserted) { emit trackImportFailed(track.path, "database insert failed"); return std::nullopt; }
    if (inserted->id < 0) return inserted; // already existed (path UNIQUE) — not an error

    const QByteArray picture = extractEmbeddedPicture(track.path, track.format);
    if (!picture.isEmpty()) {
        const QString cachedPath = m_covers.storeTrackCoverBytes(inserted->id, picture);
        if (!cachedPath.isEmpty()) {
            m_db.setTrackCover(inserted->id, cachedPath, "embedded");
            inserted->coverPath = cachedPath;
            inserted->coverSource = "embedded";
        }
    }
    return inserted;
}

std::optional<Track> LibraryManager::readAndInsert(const QString &path)
{
    auto track = readTags(path);
    if (!track) return std::nullopt;
    return storeNewTrack(*track);
}

bool LibraryManager::writeTrackTags(const Track &track)
{
    const auto *wide = reinterpret_cast<const wchar_t *>(track.path.utf16());
    const QString suffix = QFileInfo(track.path).suffix().toLower();
    TagLib::FileRef ref;
    if (suffix == QLatin1String("fla")) {
        auto *flac = new TagLib::FLAC::File(wide);
        if (!flac->isValid()) {
            delete flac;
            return false;
        }
        ref = TagLib::FileRef(flac);
    } else {
        ref = TagLib::FileRef(wide);
    }
    if (ref.isNull() || !ref.tag()) return false;
    const auto text = [](const QString &value) {
        const QByteArray utf8 = value.toUtf8();
        return TagLib::String(utf8.constData(), TagLib::String::UTF8);
    };
    ref.tag()->setTitle(text(track.title));
    ref.tag()->setArtist(text(track.artist));
    ref.tag()->setAlbum(text(track.album));
    ref.tag()->setGenre(text(track.genre));
    return ref.save();
}

std::optional<Track> LibraryManager::addTrack(const QString &path)
{
    if (!isSupportedFile(path)) {
        emit trackImportFailed(path, "unsupported format");
        return std::nullopt;
    }
    return readAndInsert(path);
}

QVector<Track> LibraryManager::importFolder(const QString &folderPath, bool recursive, bool createPlaylistNamedAfterFolder)
{
    QVector<Track> imported;

    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(folderPath, QDir::Files, flags);

    QStringList allPaths;
    while (it.hasNext()) {
        const QString path = it.next();
        if (isSupportedFile(path)) allPaths << path;
    }
    allPaths.sort(); // stable, predictable track order (usually matches disc/track numbering)

    qint64 playlistId = -1;
    if (createPlaylistNamedAfterFolder && !allPaths.isEmpty()) {
        playlistId = m_db.createPlaylist(QDir(folderPath).dirName());
    }

    int done = 0;
    for (const QString &path : allPaths) {
        if (auto track = readAndInsert(path)) {
            imported.push_back(*track);
            qint64 id = track->id;
            if (id < 0) id = m_db.trackIdForPath(path);
            if (playlistId >= 0 && id >= 0) m_db.addTrackToPlaylist(playlistId, id);
        }
        emit importProgress(++done, allPaths.size());
    }

    watchFolder(folderPath, recursive, playlistId, true);
    return imported;
}

void LibraryManager::watchFolder(const QString &folderPath, bool recursive, qint64 playlistId, bool importNew)
{
    const QString path = canonicalPath(folderPath);
    if (!m_db.upsertWatchedFolder(path, recursive, playlistId, importNew)) return;
    if (!QFileInfo(path).isDir()) return;
    m_watcher.addPath(path);
    if (!recursive) return;
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sub = it.next();
        if (QFileInfo(sub).isDir()) m_watcher.addPath(sub);
    }
}

void LibraryManager::startWatching()
{
    if (m_db.getSetting(QStringLiteral("watch_backfill")).isEmpty()) {
        QSet<QString> parents;
        for (const Track &track : m_db.allTracks()) {
            const QString dir = canonicalPath(QFileInfo(track.path).absolutePath());
            if (!dir.isEmpty()) parents.insert(dir);
        }
        for (const QString &dir : parents)
            m_db.upsertWatchedFolder(dir, false, -1, false);
        m_db.setSetting(QStringLiteral("watch_backfill"), QStringLiteral("1"));
    }

    for (const WatchedFolder &folder : m_db.watchedFolders()) {
        if (!QFileInfo(folder.path).isDir()) continue;
        m_watcher.addPath(folder.path);
        if (!folder.recursive) continue;
        QDirIterator it(folder.path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) m_watcher.addPath(it.next());
    }

    m_fullRescan = true;
    m_rescanTimer.start();
}

void LibraryManager::onDirectoryChanged(const QString &path)
{
    m_dirty.insert(pathKey(path));
    m_rescanTimer.start();
}

void LibraryManager::rescanDirty()
{
    if (m_scanning) return;
    m_scanning = true;

    const bool full = m_fullRescan;
    m_fullRescan = false;
    const QSet<QString> dirty = m_dirty;
    m_dirty.clear();

    QVector<WatchedFolder> active;
    for (const WatchedFolder &folder : m_db.watchedFolders()) {
        if (!QFileInfo(folder.path).isDir()) continue;
        if (!full && !dirty.contains(pathKey(folder.path))) continue;
        active.push_back(folder);
    }

    if (!active.isEmpty()) {
        QHash<QString, QString> onDisk;
        for (const WatchedFolder &folder : active) {
            const auto flags = folder.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
            QDirIterator it(folder.path, QDir::Files, flags);
            while (it.hasNext()) {
                const QString path = it.next();
                if (!isSupportedFile(path)) continue;
                onDisk.insert(pathKey(path), canonicalPath(path));
            }
        }

        const QVector<Track> library = m_db.allTracks();
        QHash<QString, Track> byKey;
        for (const Track &track : library)
            byKey.insert(pathKey(track.path), track);

        QHash<qint64, Track> missingById;
        for (const WatchedFolder &folder : active) {
            for (const Track &track : library) {
                if (!pathInFolder(track.path, folder.path, folder.recursive)) continue;
                if (!onDisk.contains(pathKey(track.path)))
                    missingById.insert(track.id, track);
            }
        }
        QVector<Track> missing;
        missing.reserve(missingById.size());
        for (const Track &track : missingById)
            missing.push_back(track);

        QStringList fresh;
        bool wantNew = false;
        for (const WatchedFolder &folder : active)
            if (folder.importNew) wantNew = true;
        if (!missing.isEmpty() || wantNew) {
            for (auto it = onDisk.cbegin(); it != onDisk.cend(); ++it) {
                if (!byKey.contains(it.key())) fresh.push_back(it.value());
            }
        }

        struct FreshFile {
            QString path;
            Track tags;
            bool ok = false;
        };
        QVector<FreshFile> news;
        news.reserve(fresh.size());
        for (const QString &path : fresh) {
            FreshFile file;
            file.path = path;
            if (auto tags = readTags(path)) {
                file.tags = *tags;
                file.tags.path = path;
                file.ok = true;
            }
            news.push_back(file);
        }

        QHash<QString, QVector<int>> missingGroups;
        for (int i = 0; i < missing.size(); ++i) {
            missingGroups[renameSignature(missing[i].title, missing[i].artist, missing[i].durationMs)].append(i);
        }
        QHash<QString, QVector<int>> newGroups;
        for (int i = 0; i < news.size(); ++i) {
            if (!news[i].ok) continue;
            newGroups[renameSignature(news[i].tags.title, news[i].tags.artist, news[i].tags.durationMs)].append(i);
        }

        QSet<int> missingUsed;
        QSet<int> newUsed;
        QList<qint64> renamedIds;
        QStringList renamedPaths;
        for (auto it = missingGroups.cbegin(); it != missingGroups.cend(); ++it) {
            const auto found = newGroups.constFind(it.key());
            if (found == newGroups.cend()) continue;
            if (it.value().size() != 1 || found.value().size() != 1) continue;
            const int missingIndex = it.value().first();
            const int newIndex = found.value().first();
            if (!m_db.updateTrackPath(missing[missingIndex].id, news[newIndex].path)) continue;
            missingUsed.insert(missingIndex);
            newUsed.insert(newIndex);
            renamedIds.push_back(missing[missingIndex].id);
            renamedPaths.push_back(news[newIndex].path);
        }

        QList<qint64> removedIds;
        for (int i = 0; i < missing.size(); ++i) {
            if (missingUsed.contains(i)) continue;
            const Track &track = missing[i];
            const bool dropCover = !track.coverPath.isEmpty()
                && !m_db.coverStillUsed(track.coverPath, track.id);
            if (!m_db.deleteTrack(track.id)) continue;
            if (dropCover) QFile::remove(track.coverPath);
            removedIds.push_back(track.id);
        }

        int added = 0;
        for (int i = 0; i < news.size(); ++i) {
            if (newUsed.contains(i) || !news[i].ok) continue;
            bool allowNew = false;
            for (const WatchedFolder &folder : active) {
                if (!folder.importNew) continue;
                if (!pathInFolder(news[i].path, folder.path, folder.recursive)) continue;
                allowNew = true;
                break;
            }
            if (!allowNew) continue;
            auto inserted = storeNewTrack(news[i].tags);
            if (!inserted || inserted->id < 0) continue;
            ++added;
            for (const WatchedFolder &folder : active) {
                if (folder.playlistId < 0) continue;
                if (!pathInFolder(news[i].path, folder.path, folder.recursive)) continue;
                m_db.addTrackToPlaylist(folder.playlistId, inserted->id);
                break;
            }
        }

        if (added > 0 || !removedIds.isEmpty() || !renamedIds.isEmpty())
            emit libraryFoldersChanged(added, removedIds, renamedIds, renamedPaths);
    }

    m_scanning = false;
    if (m_fullRescan || !m_dirty.isEmpty())
        m_rescanTimer.start();
}
