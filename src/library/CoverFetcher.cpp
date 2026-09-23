#include "CoverFetcher.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace {

bool looseMatch(const QString &a, const QString &b)
{
    const QString left = a.trimmed();
    const QString right = b.trimmed();
    if (left.isEmpty() || right.isEmpty()) return false;
    return left.contains(right, Qt::CaseInsensitive) || right.contains(left, Qt::CaseInsensitive);
}

// Shop tags often append the site name. MusicBrainz and iTunes will not
// match "The Summer Ends [muzmo.ru]" against the real release title.
QString searchable(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)")));
    text.remove(QRegularExpression(QStringLiteral("(?i)https?://\\S+")));
    text.remove(QRegularExpression(QStringLiteral("(?i)\\b[\\w.-]+\\.(?:ru|com|net|org|su)\\b")));
    return text.simplified();
}

} // namespace

CoverFetcher::CoverFetcher(Database &db, CoverCache &covers, QObject *parent)
    : QObject(parent), m_db(db), m_covers(covers)
{
    connect(&m_net, &QNetworkAccessManager::finished, this, &CoverFetcher::onFinished);
}

CoverFetcher::~CoverFetcher()
{
    disconnect(&m_net, nullptr, this, nullptr);
    if (m_reply) m_reply->abort();
}

QString CoverFetcher::jobKey(const Track &track)
{
    if (!track.album.trimmed().isEmpty())
        return track.artist.trimmed().toLower() + QLatin1Char('\n') + track.album.trimmed().toLower();
    return track.artist.trimmed().toLower() + QStringLiteral("\n#\n") + track.title.trimmed().toLower();
}

QString CoverFetcher::escapeLucene(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return text;
}

void CoverFetcher::setStatus(const QString &text)
{
    if (m_status == text) return;
    m_status = text;
    emit lookupStatus(text);
}

void CoverFetcher::enqueueMissing()
{
    const QVector<Track> missing = m_db.tracksMissingCovers();
    for (const Track &track : missing) enqueueTrack(track, false);
}

void CoverFetcher::forceEnqueue(const Track &track)
{
    if (track.id < 0) return;
    m_db.setTrackCover(track.id, QString(), QString());
    Track copy = track;
    copy.coverPath.clear();
    copy.coverSource.clear();
    enqueueTrack(copy, true);
}

void CoverFetcher::enqueueTrack(const Track &track, bool bypassLocal)
{
    if (track.id < 0 || !track.coverPath.isEmpty()) return;
    if (!bypassLocal && track.coverSource == QLatin1String("unavailable")) return;

    const QString key = jobKey(track);
    auto merge = [&](Job &job) {
        if (!job.ids.contains(track.id)) job.ids.push_back(track.id);
        if (bypassLocal) job.bypassLocal = true;
        if (job.title.isEmpty()) job.title = track.title.trimmed();
    };

    for (Job &job : m_pending) {
        if (job.key == key) {
            merge(job);
            return;
        }
    }
    if (m_active && m_active->key == key) {
        merge(*m_active);
        return;
    }

    Job job;
    job.key = key;
    job.artist = searchable(track.artist);
    job.album = searchable(track.album);
    job.title = searchable(track.title);
    job.ids = {track.id};
    job.bypassLocal = bypassLocal;
    m_pending.push_back(job);
    pump();
}

void CoverFetcher::pump()
{
    if (m_active) return;
    if (m_pending.isEmpty()) {
        setStatus({});
        return;
    }
    m_active = m_pending.takeFirst();
    startActive();
}

void CoverFetcher::startActive()
{
    if (!m_active || m_active->ids.isEmpty()) {
        finishActive();
        return;
    }

    const QString release = m_active->album.isEmpty() ? m_active->title : m_active->album;
    const QString who = m_active->artist.isEmpty() ? tr("неизвестный исполнитель") : m_active->artist;
    setStatus(tr("Ищем обложку: %1 — %2").arg(who, release));

    if (!m_active->bypassLocal) {
        const QString local = m_db.findCoverForAlbum(m_active->artist, m_active->album);
        if (!local.isEmpty() && QFile::exists(local) && storeFile(local, QStringLiteral("album"))) {
            finishActive();
            return;
        }
    }

    beginDeezer();
}

void CoverFetcher::beginDeezer()
{
    if (!m_active) return;
    const auto field = [](QString value) {
        value.replace(QLatin1Char('"'), QLatin1Char(' '));
        return value.simplified();
    };
    QStringList parts;
    if (!m_active->artist.isEmpty())
        parts << QStringLiteral("artist:\"%1\"").arg(field(m_active->artist));
    if (!m_active->album.isEmpty())
        parts << QStringLiteral("album:\"%1\"").arg(field(m_active->album));
    else if (!m_active->title.isEmpty())
        parts << QStringLiteral("track:\"%1\"").arg(field(m_active->title));
    QUrl url(QStringLiteral("https://api.deezer.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
    if (!m_active->deezerPlain && !parts.isEmpty()) {
        query.addQueryItem(QStringLiteral("q"), parts.join(QLatin1Char(' ')));
        url.setQuery(query);
        request(url, QStringLiteral("deezer"), false);
        return;
    }
    const QString plain = (m_active->artist + QLatin1Char(' ')
                           + (m_active->album.isEmpty() ? m_active->title : m_active->album)).simplified();
    if (plain.isEmpty()) {
        beginItunes();
        return;
    }
    query.addQueryItem(QStringLiteral("q"), plain);
    url.setQuery(query);
    request(url, QStringLiteral("deezer-plain"), false);
}

void CoverFetcher::beginMusicBrainz()
{
    if (!m_active) return;

    QStringList parts;
    if (!m_active->album.isEmpty()) {
        parts << QStringLiteral("release:\"%1\"").arg(escapeLucene(m_active->album));
        if (!m_active->artist.isEmpty())
            parts << QStringLiteral("artist:\"%1\"").arg(escapeLucene(m_active->artist));
    } else {
        parts << QStringLiteral("recording:\"%1\"").arg(escapeLucene(m_active->title));
        if (!m_active->artist.isEmpty())
            parts << QStringLiteral("artist:\"%1\"").arg(escapeLucene(m_active->artist));
    }
    if (parts.isEmpty()) {
        failActive(true);
        return;
    }

    const bool releaseSearch = !m_active->album.isEmpty();
    QUrl url(releaseSearch ? QStringLiteral("https://musicbrainz.org/ws/2/release/")
                           : QStringLiteral("https://musicbrainz.org/ws/2/recording/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("query"), parts.join(QStringLiteral(" AND ")));
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("limit"), releaseSearch ? QStringLiteral("3") : QStringLiteral("1"));
    url.setQuery(query);
    request(url, releaseSearch ? QStringLiteral("mb-release") : QStringLiteral("mb-recording"), true);
}

void CoverFetcher::beginCoverArt()
{
    if (!m_active) return;
    if (m_active->releaseCursor >= m_active->releaseIds.size()) {
        failActive(true);
        return;
    }
    const QString mbid = m_active->releaseIds.at(m_active->releaseCursor);
    const QString leaf = m_active->tryFullImage ? QStringLiteral("front") : QStringLiteral("front-500");
    const QUrl url(QStringLiteral("https://coverartarchive.org/release/%1/%2").arg(mbid, leaf));
    request(url, QStringLiteral("caa"), false);
}

void CoverFetcher::beginItunes()
{
    if (!m_active) return;
    const QString term = (m_active->artist + QLatin1Char(' ')
                          + (m_active->album.isEmpty() ? m_active->title : m_active->album)).simplified();
    if (term.isEmpty()) {
        beginMusicBrainz();
        return;
    }

    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"), term);
    query.addQueryItem(QStringLiteral("media"), QStringLiteral("music"));
    query.addQueryItem(QStringLiteral("entity"), m_active->album.isEmpty() ? QStringLiteral("song") : QStringLiteral("album"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("8"));
    url.setQuery(query);
    request(url, QStringLiteral("itunes"), false);
}

void CoverFetcher::request(const QUrl &url, const QString &phase, bool rateLimit)
{
    const int generation = m_generation;
    const auto send = [this, url, phase, generation]() {
        if (!m_active || generation != m_generation || m_reply) return;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SandPlay/1.0 (desktop music player; cover lookup)"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(20000);
        if (phase.startsWith(QLatin1String("mb")) || phase == QLatin1String("itunes")
            || phase == QLatin1String("deezer"))
            req.setRawHeader("Accept", "application/json");
        m_reply = m_net.get(req);
        m_reply->setProperty("phase", phase);
        if (phase.startsWith(QLatin1String("mb"))) m_lastMusicBrainz.start();
    };

    if (rateLimit && m_lastMusicBrainz.isValid() && m_lastMusicBrainz.elapsed() < 1100)
        QTimer::singleShot(int(1100 - m_lastMusicBrainz.elapsed()), this, send);
    else
        send();
}

bool CoverFetcher::storeBytes(const QByteArray &bytes, const QString &source)
{
    if (!m_active || bytes.isEmpty()) return false;
    bool any = false;
    for (qint64 id : m_active->ids) {
        const QString path = m_covers.storeTrackCoverBytes(id, bytes);
        if (path.isEmpty()) continue;
        m_db.setTrackCover(id, path, source);
        emit coverReady(id, path);
        any = true;
    }
    return any;
}

bool CoverFetcher::storeFile(const QString &path, const QString &source)
{
    if (!m_active || path.isEmpty()) return false;
    bool any = false;
    for (qint64 id : m_active->ids) {
        const QString stored = m_covers.storeTrackCoverFromFile(id, path);
        if (stored.isEmpty()) continue;
        m_db.setTrackCover(id, stored, source);
        emit coverReady(id, stored);
        any = true;
    }
    return any;
}

void CoverFetcher::failActive(bool rememberMiss)
{
    if (m_active && rememberMiss) {
        for (qint64 id : m_active->ids)
            m_db.setTrackCover(id, QString(), QStringLiteral("unavailable"));
        setStatus(tr("Обложка не найдена"));
    } else if (m_active) {
        setStatus(tr("Сервер обложек не ответил. Поиск можно повторить."));
    }
    finishActive();
}

void CoverFetcher::finishActive()
{
    ++m_generation;
    m_active.reset();
    pump();
}

void CoverFetcher::onFinished(QNetworkReply *reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;

    const QString phase = reply->property("phase").toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString errorString = reply->errorString();
        const bool transportError = reply->error() != QNetworkReply::NoError && status == 0;
        reply->deleteLater();

        if (!m_active) return;

        if (transportError) {
            qWarning() << "cover lookup failed:" << errorString;
            if (phase == QLatin1String("deezer")) {
                m_active->deezerPlain = true;
                beginDeezer();
            } else if (phase == QLatin1String("deezer-plain")) beginItunes();
            else if (phase == QLatin1String("itunes") || phase == QLatin1String("image-deezer")) beginMusicBrainz();
            else failActive(false);
            return;
        }

        if (phase == QLatin1String("deezer") || phase == QLatin1String("deezer-plain")) {
            const bool plain = phase == QLatin1String("deezer-plain");
            if (status < 200 || status >= 300) {
                if (!plain) {
                    m_active->deezerPlain = true;
                    beginDeezer();
                } else {
                    beginItunes();
                }
                return;
            }
            m_active->deezerAnswered = true;
            const QJsonArray data = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
            QString bestArt;
            int bestScore = -1;
            bool bestArtist = m_active->artist.isEmpty();
            for (const QJsonValue &value : data) {
                const QJsonObject object = value.toObject();
                const QJsonObject album = object.value(QStringLiteral("album")).toObject();
                QString art = album.value(QStringLiteral("cover_xl")).toString();
                if (art.isEmpty()) art = album.value(QStringLiteral("cover_big")).toString();
                if (art.isEmpty()) continue;
                const bool artistHit = looseMatch(
                    object.value(QStringLiteral("artist")).toObject().value(QStringLiteral("name")).toString(),
                    m_active->artist);
                int score = 0;
                if (artistHit) score += 2;
                if (looseMatch(album.value(QStringLiteral("title")).toString(), m_active->album)) score += 3;
                if (looseMatch(object.value(QStringLiteral("title")).toString(), m_active->title)) score += 2;
                if (score > bestScore) {
                    bestScore = score;
                    bestArt = art;
                    bestArtist = m_active->artist.isEmpty() || artistHit;
                }
            }
            const int needed = m_active->artist.isEmpty() && m_active->album.isEmpty() ? 1 : 2;
            if (bestScore < needed || bestArt.isEmpty() || (plain && !bestArtist)) {
                if (!plain) {
                    m_active->deezerPlain = true;
                    beginDeezer();
                } else {
                    beginItunes();
                }
            } else {
                request(QUrl(bestArt), QStringLiteral("image-deezer"), false);
            }
            return;
        }

        if (phase == QLatin1String("mb-release") || phase == QLatin1String("mb-recording")) {
            if (status == 503 && m_active->retries < 2) {
                ++m_active->retries;
                const int generation = m_generation;
                QTimer::singleShot(2000, this, [this, generation]() {
                    if (generation != m_generation || !m_active) return;
                    beginMusicBrainz();
                });
                return;
            }
            if (status < 200 || status >= 300) {
                failActive(m_active->deezerAnswered && m_active->itunesAnswered);
                return;
            }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        if (phase == QLatin1String("mb-release")) {
            const QJsonArray releases = root.value(QStringLiteral("releases")).toArray();
            for (const QJsonValue &value : releases) {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) m_active->releaseIds.push_back(id);
            }
        } else {
            const QJsonArray recordings = root.value(QStringLiteral("recordings")).toArray();
            if (!recordings.isEmpty()) {
                const QJsonArray releases = recordings.first().toObject().value(QStringLiteral("releases")).toArray();
                for (const QJsonValue &value : releases) {
                    const QString id = value.toObject().value(QStringLiteral("id")).toString();
                    if (!id.isEmpty()) m_active->releaseIds.push_back(id);
                    if (m_active->releaseIds.size() >= 3) break;
                }
            }
        }
        m_active->releaseCursor = 0;
        m_active->tryFullImage = false;
        beginCoverArt();
        return;
    }

    if (phase == QLatin1String("caa")) {
        QImage image;
        if (status >= 200 && status < 300 && image.loadFromData(body)) {
            if (storeBytes(body, QStringLiteral("caa"))) finishActive();
            else failActive(true);
            return;
        }
        if (!m_active->tryFullImage) {
            m_active->tryFullImage = true;
            beginCoverArt();
            return;
        }
        m_active->tryFullImage = false;
        ++m_active->releaseCursor;
        beginCoverArt();
        return;
    }

    if (phase == QLatin1String("itunes")) {
        if (status >= 200 && status < 300) m_active->itunesAnswered = true;
        const QJsonArray results = QJsonDocument::fromJson(body).object().value(QStringLiteral("results")).toArray();
        QString bestArt;
        int bestScore = -1;
        for (const QJsonValue &value : results) {
            const QJsonObject object = value.toObject();
            QString art = object.value(QStringLiteral("artworkUrl100")).toString();
            if (art.isEmpty()) art = object.value(QStringLiteral("artworkUrl60")).toString();
            if (art.isEmpty()) continue;
            int score = 0;
            if (looseMatch(object.value(QStringLiteral("artistName")).toString(), m_active->artist)) score += 2;
            if (looseMatch(object.value(QStringLiteral("collectionName")).toString(), m_active->album)) score += 3;
            if (looseMatch(object.value(QStringLiteral("trackName")).toString(), m_active->title)
                || looseMatch(object.value(QStringLiteral("collectionName")).toString(), m_active->title))
                score += 1;
            if (score > bestScore) {
                bestScore = score;
                bestArt = art;
            }
        }

        const int needed = m_active->artist.isEmpty() && m_active->album.isEmpty() ? 1 : 2;
        if (bestScore < needed || bestArt.isEmpty()) {
            beginMusicBrainz();
            return;
        }
        bestArt.replace(QStringLiteral("100x100bb"), QStringLiteral("600x600bb"));
        request(QUrl(bestArt), QStringLiteral("image-itunes"), false);
        return;
    }

    if (phase == QLatin1String("image-deezer") || phase == QLatin1String("image-itunes")) {
        QImage image;
        const QString source = phase.endsWith(QLatin1String("deezer")) ? QStringLiteral("deezer")
                                                                        : QStringLiteral("itunes");
        if (status >= 200 && status < 300 && image.loadFromData(body) && storeBytes(body, source))
            finishActive();
        else if (phase == QLatin1String("image-deezer"))
            beginItunes();
        else
            beginMusicBrainz();
    }
}
