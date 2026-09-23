#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <memory>

struct ma_engine;
struct ma_sound;

// Wraps miniaudio's ma_engine/ma_sound. miniaudio decodes wav/flac/mp3
// itself (via bundled dr_wav/dr_flac/minimp3) and streams from disk —
// nothing here ever holds a whole decoded track in memory.
//
// Position/duration come straight from the decoder's PCM frame cursor, so
// the progress bar in the UI reflects real playback position, not a
// timer-based estimate, and seeking is sample-accurate.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool init();                         // sets up the ma_engine + output device once
    bool loadAndPlay(const QString &path);
    void pause();
    void resume();
    void stop();

    void seekToMs(qint64 ms);
    void setVolume(float volume01);      // 0.0–1.0, remembered even when nothing is loaded
    float volume() const { return m_volume; }

    // Ten peaking bands, 32 Hz … 16 kHz. Gain is decibels, clamped to ±12.
    static constexpr int kEqBands = 10;
    void setEqualizerGain(int band, float db);
    float equalizerGain(int band) const;
    QString equalizerBandLabel(int band) const;

    qint64 positionMs() const;
    qint64 durationMs() const;
    bool isPlaying() const;

signals:
    void positionChanged(qint64 ms);     // emitted ~4x/sec while playing
    void trackFinished();                // reached end of stream — PlaybackQueue advances
    void playbackStateChanged(bool playing);
    void errorOccurred(const QString &message);

private slots:
    void pollProgress();                 // QTimer tick: checks position + end-of-stream

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QTimer m_pollTimer;
    bool m_wasAtEnd = false;
    float m_volume = 0.8f;
};
