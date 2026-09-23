#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVector>
#include <optional>

#include "../db/Database.h"
#include "CoverCache.h"

class QNetworkReply;

// Fills in missing album art and stores it in CoverCache.
//
// Order:
//   1. Another track of the same album that already has a local image.
//   2. Deezer search (public, no key) — cover_xl from the matching album.
//      A quoted artist/album query is tried first. If Deezer returns nothing,
//      the same words are sent as a plain search.
//   3. Apple iTunes Search API.
//   4. MusicBrainz search + Cover Art Archive.
//
// Queries drop shop tags such as "[muzmo.ru]" before they are sent.
// A real miss is remembered as cover_source = "unavailable". A transport
// error is not, so the next launch can try again.
// MusicBrainz asks for at most one request per second; that wait is enforced here.
class CoverFetcher : public QObject {
    Q_OBJECT
public:
    CoverFetcher(Database &db, CoverCache &covers, QObject *parent = nullptr);
    ~CoverFetcher() override;

    void enqueueMissing();
    void enqueueTrack(const Track &track, bool bypassLocal = false);
    // Clears a previous miss (or an existing image the user wants replaced) and looks again.
    void forceEnqueue(const Track &track);

    QString status() const { return m_status; }

signals:
    void coverReady(qint64 trackId, const QString &path);
    void lookupStatus(const QString &text);

private:
    struct Job {
        QString key;
        QString artist;
        QString album;
        QString title;
        QVector<qint64> ids;
        QStringList releaseIds;
        int releaseCursor = 0;
        bool tryFullImage = false;
        bool bypassLocal = false;
        bool deezerAnswered = false;
        bool deezerPlain = false;
        bool itunesAnswered = false;
        int retries = 0;
    };

    Database &m_db;
    CoverCache &m_covers;
    QNetworkAccessManager m_net;
    QVector<Job> m_pending;
    std::optional<Job> m_active;
    QNetworkReply *m_reply = nullptr;
    QElapsedTimer m_lastMusicBrainz;
    int m_generation = 0;
    QString m_status;

    void pump();
    void startActive();
    void beginDeezer();
    void beginMusicBrainz();
    void beginCoverArt();
    void beginItunes();
    void finishActive();
    void failActive(bool rememberMiss);
    void request(const QUrl &url, const QString &phase, bool rateLimit);
    void onFinished(QNetworkReply *reply);

    bool storeBytes(const QByteArray &bytes, const QString &source);
    bool storeFile(const QString &path, const QString &source);
    void setStatus(const QString &text);
    static QString jobKey(const Track &track);
    static QString escapeLucene(QString text);
};
