#pragma once
#include <QElapsedTimer>
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

    // 0 sample rate keeps the device's native rate. 0 bit depth is 32-bit float.
    // Any other pair opens the device at that rate and 16/24/32-bit integer.
    void setOutputPreference(int sampleRate, int bitDepth);
    bool init();
    // Rebuilds the device. A track that is loaded keeps its place.
    bool configureOutput(int sampleRate, int bitDepth);
    int sampleRate() const { return m_sampleRate; }
    int bitDepth() const { return m_bitDepth; }
    bool loadAndPlay(const QString &path);
    // When on, the following track is opened in a second streamed sound and
    // started on the engine clock at the last frame of the current one.
    void setGapless(bool on);
    void prepareNext(const QString &path);
    void clearNext();
    QString loadedPath() const { return m_loadedPath; }
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
    void trackFinished();                // current track ended; gapless may already be inside the next one
    void playbackStateChanged(bool playing);
    void errorOccurred(const QString &message);

private slots:
    void pollProgress();                 // QTimer tick: checks position + end-of-stream

private:
    void shutdown();
    bool initNative();
    bool initCustom();
    bool attachEqualizer();
    void restorePlayback(const QString &path, qint64 positionMs, bool playing);
    void scheduleNext(bool rewind);
    void adoptNext(bool markHandoff);
    void uninitSlot(int slot);
    bool openSlot(int slot, const QString &path);
    void attachSound(ma_sound *sound);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QTimer m_pollTimer;
    QElapsedTimer m_boundaryWait;
    bool m_wasAtEnd = false;
    bool m_seamlessHandoff = false;
    float m_volume = 0.8f;
    int m_sampleRate = 0;
    int m_bitDepth = 0;
    QString m_loadedPath;
};
