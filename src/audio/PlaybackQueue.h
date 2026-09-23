#pragma once
#include <QObject>
#include <QVector>
#include <optional>
#include "../db/Database.h"

// Auto-advance target. Holds an ordered list of tracks plus a cursor;
// AudioEngine::trackFinished should be wired to advance() by whoever
// owns both (see MainWindow).
class PlaybackQueue : public QObject {
    Q_OBJECT
public:
    enum class RepeatMode { Off, RepeatOne, RepeatAll };
    Q_ENUM(RepeatMode)

    explicit PlaybackQueue(QObject *parent = nullptr);

    void setTracks(const QVector<Track> &tracks, int startIndex = 0);
    void enqueueNext(const Track &t);   // "play next" — inserted right after current
    void append(const Track &t);        // "add to queue" — inserted at the end
    void insertTracks(int index, const QVector<Track> &tracks);
    void removeAt(int index);
    void removeIds(const QList<qint64> &ids);
    void moveTrack(int from, int to);   // user drag-reorders the queue in the UI
    void clear();
    void updateCover(qint64 trackId, const QString &coverPath);
    void updateDetails(const Track &incoming);
    void updatePath(qint64 trackId, const QString &path);

    void setShuffle(bool on);
    bool shuffle() const { return m_shuffle; }
    void setRepeatMode(RepeatMode mode);
    RepeatMode repeatMode() const { return m_repeatMode; }

    std::optional<Track> current() const;
    const QVector<Track>& tracks() const { return m_tracks; }
    int currentIndex() const { return m_currentIndex; }

public slots:
    std::optional<Track> advance();   // end of track: honors repeat-one
    std::optional<Track> skipNext();  // next button: leaves the current track even in repeat-one
    std::optional<Track> previous();
    std::optional<Track> jumpTo(int index);

signals:
    void queueChanged();
    void currentChanged(std::optional<Track> track);

private:
    QVector<Track> m_tracks;
    QVector<int> m_shuffleOrder;   // permutation of indices into m_tracks, rebuilt on setShuffle/setTracks
    int m_currentIndex = -1;       // index into m_tracks (not m_shuffleOrder)
    bool m_shuffle = false;
    RepeatMode m_repeatMode = RepeatMode::Off;

    void rebuildShuffleOrder();
    int shufflePositionOf(int trackIndex) const;
};
