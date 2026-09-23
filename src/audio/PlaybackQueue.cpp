#include "PlaybackQueue.h"
#include <QSet>
#include <algorithm>
#include <random>

PlaybackQueue::PlaybackQueue(QObject *parent) : QObject(parent) {}

void PlaybackQueue::setTracks(const QVector<Track> &tracks, int startIndex)
{
    m_tracks = tracks;
    m_currentIndex = tracks.isEmpty() ? -1 : std::clamp(startIndex, 0, static_cast<int>(tracks.size()) - 1);
    rebuildShuffleOrder();
    emit queueChanged();
    emit currentChanged(current());
}

void PlaybackQueue::enqueueNext(const Track &t)
{
    const int insertAt = m_currentIndex < 0 ? 0 : m_currentIndex + 1;
    m_tracks.insert(insertAt, t);
    if (m_currentIndex >= insertAt) m_currentIndex++; // keep pointing at the same track
    rebuildShuffleOrder();
    emit queueChanged();
}

void PlaybackQueue::append(const Track &t)
{
    m_tracks.append(t);
    rebuildShuffleOrder();
    emit queueChanged();
}

void PlaybackQueue::insertTracks(int index, const QVector<Track> &tracks)
{
    if (tracks.isEmpty()) return;
    if (index < 0 || index > m_tracks.size()) index = m_tracks.size();
    for (int i = 0; i < tracks.size(); ++i) m_tracks.insert(index + i, tracks[i]);
    if (m_currentIndex >= index) m_currentIndex += tracks.size();
    rebuildShuffleOrder();
    emit queueChanged();
}

void PlaybackQueue::removeAt(int index)
{
    if (index < 0 || index >= m_tracks.size()) return;
    const bool removingCurrent = index == m_currentIndex;
    m_tracks.removeAt(index);
    if (removingCurrent) {
        if (m_tracks.isEmpty()) m_currentIndex = -1;
        else m_currentIndex = std::min(index, static_cast<int>(m_tracks.size()) - 1);
    } else if (m_currentIndex > index) {
        m_currentIndex--;
    }
    rebuildShuffleOrder();
    emit queueChanged();
    if (removingCurrent) emit currentChanged(current());
}

void PlaybackQueue::removeIds(const QList<qint64> &ids)
{
    if (ids.isEmpty() || m_tracks.isEmpty()) return;
    const QSet<qint64> drop(ids.begin(), ids.end());
    const bool hadCurrent = m_currentIndex >= 0 && m_currentIndex < m_tracks.size();
    const qint64 currentId = hadCurrent ? m_tracks[m_currentIndex].id : -1;

    QVector<Track> kept;
    kept.reserve(m_tracks.size());
    int keptBeforeCurrent = 0;
    bool removedCurrent = false;
    int newCurrent = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (drop.contains(m_tracks[i].id)) {
            if (i == m_currentIndex) removedCurrent = true;
            continue;
        }
        if (i == m_currentIndex) newCurrent = kept.size();
        if (hadCurrent && i < m_currentIndex) ++keptBeforeCurrent;
        kept.push_back(m_tracks[i]);
    }
    if (kept.size() == m_tracks.size()) return;

    m_tracks = kept;
    if (m_tracks.isEmpty()) m_currentIndex = -1;
    else if (removedCurrent) m_currentIndex = std::min(keptBeforeCurrent, int(m_tracks.size()) - 1);
    else m_currentIndex = newCurrent;

    rebuildShuffleOrder();
    emit queueChanged();
    if (removedCurrent || (hadCurrent && currentId != (current() ? current()->id : -1)))
        emit currentChanged(current());
}

void PlaybackQueue::clear()
{
    if (m_tracks.isEmpty() && m_currentIndex < 0) {
        emit queueChanged();
        return;
    }
    m_tracks.clear();
    m_shuffleOrder.clear();
    m_currentIndex = -1;
    emit queueChanged();
    emit currentChanged(std::nullopt);
}

void PlaybackQueue::updateCover(qint64 trackId, const QString &coverPath)
{
    bool changed = false;
    for (Track &t : m_tracks) {
        if (t.id != trackId || t.coverPath == coverPath) continue;
        t.coverPath = coverPath;
        changed = true;
    }
    if (changed) emit queueChanged();
}

void PlaybackQueue::updatePath(qint64 trackId, const QString &path)
{
    bool changed = false;
    for (Track &track : m_tracks) {
        if (track.id != trackId || track.path == path) continue;
        track.path = path;
        changed = true;
    }
    if (changed) emit queueChanged();
}

void PlaybackQueue::updateDetails(const Track &incoming)
{
    for (Track &track : m_tracks) {
        if (track.id != incoming.id) continue;
        track.title = incoming.title;
        track.artist = incoming.artist;
        track.album = incoming.album;
        track.genre = incoming.genre;
    }
    emit queueChanged();
}

void PlaybackQueue::moveTrack(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_tracks.size() || to >= m_tracks.size()) return;
    m_tracks.move(from, to);
    if (m_currentIndex == from) m_currentIndex = to;
    else if (from < m_currentIndex && to >= m_currentIndex) m_currentIndex--;
    else if (from > m_currentIndex && to <= m_currentIndex) m_currentIndex++;
    rebuildShuffleOrder();
    emit queueChanged();
}

void PlaybackQueue::setShuffle(bool on)
{
    if (m_shuffle == on) return;
    m_shuffle = on;
    rebuildShuffleOrder();
}

void PlaybackQueue::setRepeatMode(RepeatMode mode)
{
    m_repeatMode = mode;
}

std::optional<Track> PlaybackQueue::current() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_tracks.size()) return std::nullopt;
    return m_tracks[m_currentIndex];
}

std::optional<Track> PlaybackQueue::advance()
{
    if (m_tracks.isEmpty()) return std::nullopt;

    if (m_repeatMode == RepeatMode::RepeatOne) {
        emit currentChanged(current());
        return current(); // caller re-plays the same track from position 0
    }

    const int shufflePos = shufflePositionOf(m_currentIndex);
    const int nextShufflePos = shufflePos + 1;

    if (nextShufflePos >= m_shuffleOrder.size()) {
        if (m_repeatMode == RepeatMode::RepeatAll) {
            m_currentIndex = m_shuffleOrder.isEmpty() ? -1 : m_shuffleOrder.first();
            if (m_shuffle) rebuildShuffleOrder(); // fresh shuffle each lap, like most players do
        } else {
            m_currentIndex = -1; // end of queue, nothing left to play
        }
    } else {
        m_currentIndex = m_shuffleOrder[nextShufflePos];
    }

    emit currentChanged(current());
    return current();
}

std::optional<Track> PlaybackQueue::skipNext()
{
    if (m_tracks.isEmpty()) return std::nullopt;

    const int shufflePos = shufflePositionOf(m_currentIndex);
    const int nextShufflePos = shufflePos + 1;
    if (nextShufflePos >= m_shuffleOrder.size()) {
        if (m_repeatMode == RepeatMode::Off) {
            m_currentIndex = -1;
        } else {
            m_currentIndex = m_shuffleOrder.isEmpty() ? -1 : m_shuffleOrder.first();
            if (m_shuffle && m_repeatMode == RepeatMode::RepeatAll) rebuildShuffleOrder();
        }
    } else {
        m_currentIndex = m_shuffleOrder[nextShufflePos];
    }

    emit currentChanged(current());
    return current();
}

std::optional<Track> PlaybackQueue::previous()
{
    if (m_tracks.isEmpty()) return std::nullopt;
    const int shufflePos = shufflePositionOf(m_currentIndex);
    const int prevShufflePos = shufflePos - 1;
    if (prevShufflePos < 0) {
        emit currentChanged(current());
        return current(); // already at the start — restart the current track
    }
    m_currentIndex = m_shuffleOrder[prevShufflePos];
    emit currentChanged(current());
    return current();
}

std::optional<Track> PlaybackQueue::jumpTo(int index)
{
    if (index < 0 || index >= m_tracks.size()) return std::nullopt;
    m_currentIndex = index;
    emit currentChanged(current());
    return current();
}

void PlaybackQueue::rebuildShuffleOrder()
{
    m_shuffleOrder.resize(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i) m_shuffleOrder[i] = i;

    if (m_shuffle) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(m_shuffleOrder.begin(), m_shuffleOrder.end(), gen);
        // keep the currently-playing track at the front of the shuffle
        // order so "advance" doesn't immediately replay it
        const int pos = m_shuffleOrder.indexOf(m_currentIndex);
        if (pos > 0) m_shuffleOrder.move(pos, 0);
    }
}

int PlaybackQueue::shufflePositionOf(int trackIndex) const
{
    return m_shuffleOrder.indexOf(trackIndex);
}
