#include "AudioEngine.h"
#include "miniaudio.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

// Hidden behind a pimpl so miniaudio's C structs never leak into the header
// (keeps ma_engine/ma_sound out of anything that includes AudioEngine.h).
namespace {

constexpr int kBands = AudioEngine::kEqBands;
constexpr double kBandHz[kBands] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr const char *kBandLabel[kBands] = {"32", "64", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};

struct BiquadCoeff {
    float b0, b1, b2, a0, a1, a2;
};

BiquadCoeff peaking(double sampleRate, double frequency, double q, double db)
{
    const double A = std::pow(10.0, db / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cosw = std::cos(w0);
    return BiquadCoeff{
        float(1.0 + alpha * A), float(-2.0 * cosw), float(1.0 - alpha * A),
        float(1.0 + alpha / A), float(-2.0 * cosw), float(1.0 - alpha / A)};
}

int nativeSampleRate()
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    ma_device probe{};
    if (ma_device_init(nullptr, &config, &probe) != MA_SUCCESS) return 48000;
    const int rate = int(probe.sampleRate);
    ma_device_uninit(&probe);
    return rate > 0 ? rate : 48000;
}

ma_format formatForDepth(int bitDepth)
{
    switch (bitDepth) {
    case 16: return ma_format_s16;
    case 24: return ma_format_s24;
    case 32: return ma_format_s32;
    default: return ma_format_f32;
    }
}

} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    // Slot 0/1 trade places: one is the audible track, the other is only the
    // next track opened ahead of the boundary. Never more than one extra sound.
    ma_sound voices[2]{};
    bool slotLoaded[2]{};
    QString slotPath[2];
    int current = 0;
    int upcoming = -1;
    bool gapless = false;
    ma_device device{};
    ma_biquad_node bands[kBands]{};
    float gains[kBands]{};
    float scratch[2048 * 2]{};
    ma_uint32 channels = 2;
    ma_format playbackFormat = ma_format_f32;
    bool engineReady = false;
    bool bandsReady = false;
    bool deviceReady = false;

    static void playbackData(ma_device *device, void *output, const void *input, ma_uint32 frameCount);

    void retune(int band)
    {
        if (!bandsReady || band < 0 || band >= kBands) return;
        const auto c = peaking(ma_engine_get_sample_rate(&engine), kBandHz[band], 1.2, gains[band]);
        const ma_biquad_config config = ma_biquad_config_init(
            ma_format_f32, channels, c.b0, c.b1, c.b2, c.a0, c.a1, c.a2);
        ma_biquad_node_reinit(&config, &bands[band]);
    }
};

void AudioEngine::Impl::playbackData(ma_device *device, void *output, const void *, ma_uint32 frameCount)
{
    auto *impl = static_cast<AudioEngine::Impl *>(device->pUserData);
    if (!impl || !impl->engineReady || !output) return;
    const ma_uint32 channels = std::max(1u, impl->channels);
    if (impl->playbackFormat == ma_format_f32) {
        ma_engine_read_pcm_frames(&impl->engine, output, frameCount, nullptr);
        return;
    }
    constexpr ma_uint32 kChunk = 2048;
    auto *dst = static_cast<unsigned char *>(output);
    const ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(impl->playbackFormat, channels);
    ma_uint32 remaining = frameCount;
    while (remaining > 0) {
        const ma_uint32 count = std::min(remaining, kChunk);
        ma_engine_read_pcm_frames(&impl->engine, impl->scratch, count, nullptr);
        ma_convert_pcm_frames_format(dst, impl->playbackFormat, impl->scratch, ma_format_f32,
                                     count, channels, ma_dither_mode_triangle);
        dst += count * bytesPerFrame;
        remaining -= count;
    }
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    m_pollTimer.setInterval(250); // 4x/sec is plenty smooth for a seek slider
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioEngine::pollProgress);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::setOutputPreference(int sampleRate, int bitDepth)
{
    m_sampleRate = sampleRate;
    m_bitDepth = bitDepth;
}

void AudioEngine::shutdown()
{
    m_pollTimer.stop();
    if (m_impl->deviceReady)
        ma_device_stop(&m_impl->device);
    for (int slot = 0; slot < 2; ++slot) {
        if (!m_impl->slotLoaded[slot]) continue;
        ma_sound_uninit(&m_impl->voices[slot]);
        m_impl->slotLoaded[slot] = false;
        m_impl->slotPath[slot].clear();
    }
    m_impl->upcoming = -1;
    m_impl->current = 0;
    m_loadedPath.clear();
    m_seamlessHandoff = false;
    if (m_impl->bandsReady) {
        for (int i = 0; i < kBands; ++i)
            ma_biquad_node_uninit(&m_impl->bands[i], nullptr);
        m_impl->bandsReady = false;
    }
    if (m_impl->engineReady) {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineReady = false;
    }
    if (m_impl->deviceReady) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceReady = false;
    }
}

bool AudioEngine::init()
{
    shutdown();
    const bool custom = m_sampleRate != 0 || m_bitDepth != 0;
    const bool ok = custom ? initCustom() : initNative();
    if (!ok) {
        shutdown();
        return false;
    }
    if (m_impl->engineReady)
        ma_engine_set_volume(&m_impl->engine, m_volume);
    return true;
}

bool AudioEngine::configureOutput(int sampleRate, int bitDepth)
{
    const QString path = m_loadedPath;
    const qint64 position = positionMs();
    const bool playing = isPlaying();
    const int previousRate = m_sampleRate;
    const int previousDepth = m_bitDepth;
    m_sampleRate = sampleRate;
    m_bitDepth = bitDepth;
    if (!init()) {
        m_sampleRate = previousRate;
        m_bitDepth = previousDepth;
        init();
        restorePlayback(path, position, playing);
        return false;
    }
    restorePlayback(path, position, playing);
    return true;
}

bool AudioEngine::initNative()
{
    const ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result != MA_SUCCESS) {
        emit errorOccurred(QStringLiteral("audio device init failed (%1)").arg(int(result)));
        return false;
    }
    m_impl->engineReady = true;
    m_impl->playbackFormat = ma_format_f32;
    return attachEqualizer();
}

bool AudioEngine::initCustom()
{
    const int rate = m_sampleRate > 0 ? m_sampleRate : nativeSampleRate();
    const ma_format format = formatForDepth(m_bitDepth);

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.noDevice = MA_TRUE;
    engineConfig.channels = 2;
    engineConfig.sampleRate = ma_uint32(rate);
    if (ma_engine_init(&engineConfig, &m_impl->engine) != MA_SUCCESS)
        return false;
    m_impl->engineReady = true;
    m_impl->channels = 2;
    m_impl->playbackFormat = format;
    if (!attachEqualizer())
        return false;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = format;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = ma_uint32(rate);
    deviceConfig.dataCallback = Impl::playbackData;
    deviceConfig.pUserData = m_impl.get();
    deviceConfig.noPreSilencedOutputBuffer = MA_TRUE;
    if (ma_device_init(nullptr, &deviceConfig, &m_impl->device) != MA_SUCCESS)
        return false;
    m_impl->deviceReady = true;
    if (ma_device_start(&m_impl->device) != MA_SUCCESS)
        return false;
    return true;
}

bool AudioEngine::attachEqualizer()
{
    m_impl->channels = std::max(1u, ma_engine_get_channels(&m_impl->engine));
    ma_node_graph *graph = ma_engine_get_node_graph(&m_impl->engine);
    int made = 0;
    if (graph) {
        for (; made < kBands; ++made) {
            const auto c = peaking(ma_engine_get_sample_rate(&m_impl->engine), kBandHz[made], 1.2, m_impl->gains[made]);
            const ma_biquad_node_config config = ma_biquad_node_config_init(
                m_impl->channels, c.b0, c.b1, c.b2, c.a0, c.a1, c.a2);
            if (ma_biquad_node_init(graph, &config, nullptr, &m_impl->bands[made]) != MA_SUCCESS)
                break;
        }
    }
    if (made != kBands) {
        for (int i = 0; i < made; ++i)
            ma_biquad_node_uninit(&m_impl->bands[i], nullptr);
        return true;
    }
    for (int i = 0; i < kBands - 1; ++i)
        ma_node_attach_output_bus(&m_impl->bands[i], 0, &m_impl->bands[i + 1], 0);
    ma_node_attach_output_bus(&m_impl->bands[kBands - 1], 0, ma_engine_get_endpoint(&m_impl->engine), 0);
    m_impl->bandsReady = true;
    return true;
}

void AudioEngine::restorePlayback(const QString &path, qint64 position, bool playing)
{
    if (path.isEmpty() || !m_impl->engineReady) return;
    if (!loadAndPlay(path)) return;
    if (position > 0) seekToMs(position);
    if (!playing) pause();
}

void AudioEngine::attachSound(ma_sound *sound)
{
    if (m_impl->bandsReady && sound)
        ma_node_attach_output_bus(reinterpret_cast<ma_node *>(sound), 0, &m_impl->bands[0], 0);
}

void AudioEngine::uninitSlot(int slot)
{
    if (slot < 0 || slot > 1 || !m_impl->slotLoaded[slot]) return;
    ma_sound_stop(&m_impl->voices[slot]);
    ma_sound_uninit(&m_impl->voices[slot]);
    m_impl->slotLoaded[slot] = false;
    m_impl->slotPath[slot].clear();
    if (m_impl->upcoming == slot) m_impl->upcoming = -1;
}

bool AudioEngine::openSlot(int slot, const QString &path)
{
    if (slot < 0 || slot > 1 || !m_impl->engineReady) return false;
    uninitSlot(slot);
    const QByteArray pathUtf8 = path.toUtf8();
    // MA_SOUND_FLAG_STREAM: decode as playback advances, don't slurp the
    // whole file into RAM up front — this is what keeps memory flat
    // regardless of track length or format. The next track is a second
    // streamed sound, so only its first pages sit in memory ahead of time.
    const ma_result result = ma_sound_init_from_file(
        &m_impl->engine, pathUtf8.constData(),
        MA_SOUND_FLAG_STREAM, nullptr, nullptr, &m_impl->voices[slot]);
    if (result != MA_SUCCESS) {
        emit errorOccurred(QStringLiteral("failed to open %1 (%2)").arg(path).arg(int(result)));
        return false;
    }
    m_impl->slotLoaded[slot] = true;
    m_impl->slotPath[slot] = path;
    attachSound(&m_impl->voices[slot]);
    return true;
}

void AudioEngine::adoptNext(bool markHandoff)
{
    const int next = m_impl->upcoming;
    if (next < 0 || !m_impl->slotLoaded[next]) return;
    const int old = m_impl->current;
    m_impl->upcoming = -1;
    if (old != next) uninitSlot(old);
    m_impl->current = next;
    m_loadedPath = m_impl->slotPath[next];
    m_wasAtEnd = false;
    m_seamlessHandoff = markHandoff;
    m_boundaryWait.invalidate();
}

void AudioEngine::scheduleNext(bool rewind)
{
    if (!m_impl->gapless || m_impl->upcoming < 0 || !m_impl->slotLoaded[m_impl->upcoming])
        return;
    if (!m_impl->slotLoaded[m_impl->current]) return;

    ma_sound *next = &m_impl->voices[m_impl->upcoming];
    if (!rewind && ma_sound_is_playing(next)) return;
    if (rewind) {
        ma_sound_stop(next);
        ma_sound_seek_to_pcm_frame(next, 0);
    }

    ma_sound *current = &m_impl->voices[m_impl->current];
    ma_uint64 length = 0;
    if (ma_sound_get_length_in_pcm_frames(current, &length) != MA_SUCCESS || length == 0)
        return;

    // Cursor and engine time are both in the engine rate (the resource
    // manager decodes to it). Sample them without a device callback in
    // between so the scheduled start lands on the last frame.
    ma_uint64 endAt = 0;
    bool stable = false;
    for (int attempt = 0; attempt < 8 && !stable; ++attempt) {
        const ma_uint64 t0 = ma_engine_get_time_in_pcm_frames(&m_impl->engine);
        ma_uint64 cursor = 0;
        if (ma_sound_get_cursor_in_pcm_frames(current, &cursor) != MA_SUCCESS) return;
        const ma_uint64 t1 = ma_engine_get_time_in_pcm_frames(&m_impl->engine);
        if (t0 != t1) continue;
        if (cursor > length) cursor = length;
        endAt = t1 + (length - cursor);
        stable = true;
    }
    if (!stable) {
        ma_uint64 cursor = 0;
        if (ma_sound_get_cursor_in_pcm_frames(current, &cursor) != MA_SUCCESS) return;
        if (cursor > length) cursor = length;
        endAt = ma_engine_get_time_in_pcm_frames(&m_impl->engine) + (length - cursor);
    }
    const ma_uint64 now = ma_engine_get_time_in_pcm_frames(&m_impl->engine);
    if (endAt < now) endAt = now;
    ma_sound_set_start_time_in_pcm_frames(next, endAt);
    ma_sound_set_stop_time_in_pcm_frames(next, ~(ma_uint64)0);
    if (isPlaying()) ma_sound_start(next);
}

void AudioEngine::setGapless(bool on)
{
    m_impl->gapless = on;
    if (!on) clearNext();
}

void AudioEngine::clearNext()
{
    if (m_impl->upcoming < 0) return;
    uninitSlot(m_impl->upcoming);
}

void AudioEngine::prepareNext(const QString &path)
{
    if (!m_impl->engineReady || !m_impl->gapless || path.isEmpty()
        || !m_impl->slotLoaded[m_impl->current]) {
        clearNext();
        return;
    }
    const int slot = 1 - m_impl->current;
    if (m_impl->upcoming == slot && m_impl->slotLoaded[slot] && m_impl->slotPath[slot] == path) {
        scheduleNext(false);
        return;
    }
    clearNext();
    if (!openSlot(slot, path)) return;
    m_impl->upcoming = slot;
    scheduleNext(false);
}

bool AudioEngine::loadAndPlay(const QString &path)
{
    if (!m_impl->engineReady) return false;

    if (m_seamlessHandoff && m_loadedPath == path && isPlaying()) {
        m_seamlessHandoff = false;
        m_wasAtEnd = false;
        m_boundaryWait.invalidate();
        m_pollTimer.start();
        return true;
    }
    m_seamlessHandoff = false;

    const int queued = m_impl->upcoming;
    if (queued >= 0 && m_impl->slotLoaded[queued] && m_impl->slotPath[queued] == path) {
        const bool already = ma_sound_is_playing(&m_impl->voices[queued]);
        if (!already) {
            ma_sound_stop(&m_impl->voices[queued]);
            ma_sound_set_start_time_in_pcm_frames(&m_impl->voices[queued], 0);
            ma_sound_set_stop_time_in_pcm_frames(&m_impl->voices[queued], ~(ma_uint64)0);
            ma_sound_seek_to_pcm_frame(&m_impl->voices[queued], 0);
        }
        adoptNext(false);
        if (!isPlaying())
            ma_sound_start(&m_impl->voices[m_impl->current]);
        m_wasAtEnd = false;
        m_boundaryWait.invalidate();
        m_pollTimer.start();
        emit playbackStateChanged(true);
        return true;
    }

    clearNext();
    if (!openSlot(m_impl->current, path)) {
        m_loadedPath.clear();
        return false;
    }
    m_loadedPath = path;
    m_wasAtEnd = false;
    m_boundaryWait.invalidate();
    ma_sound_start(&m_impl->voices[m_impl->current]);
    m_pollTimer.start();
    emit playbackStateChanged(true);
    return true;
}

void AudioEngine::pause()
{
    if (!m_impl->slotLoaded[m_impl->current]) return;
    ma_sound_stop(&m_impl->voices[m_impl->current]); // miniaudio's "stop" just halts the cursor, doesn't rewind
    if (m_impl->upcoming >= 0 && m_impl->slotLoaded[m_impl->upcoming])
        ma_sound_stop(&m_impl->voices[m_impl->upcoming]);
    m_pollTimer.stop();
    m_boundaryWait.invalidate();
    emit playbackStateChanged(false);
}

void AudioEngine::resume()
{
    if (!m_impl->slotLoaded[m_impl->current]) return;
    ma_sound_start(&m_impl->voices[m_impl->current]);
    scheduleNext(false);
    m_pollTimer.start();
    emit playbackStateChanged(true);
}

void AudioEngine::stop()
{
    const bool had = m_impl->slotLoaded[0] || m_impl->slotLoaded[1];
    m_boundaryWait.invalidate();
    m_seamlessHandoff = false;
    clearNext();
    uninitSlot(m_impl->current);
    m_loadedPath.clear();
    m_pollTimer.stop();
    if (had) emit playbackStateChanged(false);
}

void AudioEngine::seekToMs(qint64 ms)
{
    if (!m_impl->slotLoaded[m_impl->current]) return;
    ma_sound *sound = &m_impl->voices[m_impl->current];
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return;
    const ma_uint64 frame = static_cast<ma_uint64>(ms) * sampleRate / 1000;
    ma_sound_seek_to_pcm_frame(sound, frame);
    m_wasAtEnd = false;
    m_boundaryWait.invalidate();
    scheduleNext(true);
}

void AudioEngine::setVolume(float volume01)
{
    m_volume = std::clamp(volume01, 0.0f, 1.0f);
    if (m_impl->engineReady)
        ma_engine_set_volume(&m_impl->engine, m_volume);
}

void AudioEngine::setEqualizerGain(int band, float db)
{
    if (band < 0 || band >= kBands) return;
    m_impl->gains[band] = std::clamp(db, -12.0f, 12.0f);
    m_impl->retune(band);
}

float AudioEngine::equalizerGain(int band) const
{
    if (band < 0 || band >= kBands) return 0.0f;
    return m_impl->gains[band];
}

QString AudioEngine::equalizerBandLabel(int band) const
{
    if (band < 0 || band >= kBands) return {};
    return QString::fromLatin1(kBandLabel[band]);
}

qint64 AudioEngine::positionMs() const
{
    if (!m_impl->slotLoaded[m_impl->current]) return 0;
    ma_sound *sound = const_cast<ma_sound *>(&m_impl->voices[m_impl->current]);
    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(sound, &cursor);
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return 0;
    return static_cast<qint64>(cursor) * 1000 / sampleRate;
}

qint64 AudioEngine::durationMs() const
{
    if (!m_impl->slotLoaded[m_impl->current]) return 0;
    ma_sound *sound = const_cast<ma_sound *>(&m_impl->voices[m_impl->current]);
    ma_uint64 frames = 0;
    ma_sound_get_length_in_pcm_frames(sound, &frames);
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return 0;
    return static_cast<qint64>(frames) * 1000 / sampleRate;
}

bool AudioEngine::isPlaying() const
{
    return m_impl->slotLoaded[m_impl->current]
        && ma_sound_is_playing(&m_impl->voices[m_impl->current]);
}

void AudioEngine::pollProgress()
{
    const int current = m_impl->current;
    if (!m_impl->slotLoaded[current]) return;

    const int next = m_impl->upcoming;
    const bool haveNext = m_impl->gapless && next >= 0 && m_impl->slotLoaded[next];
    const bool atEnd = ma_sound_at_end(&m_impl->voices[current]);
    if (haveNext && atEnd) {
        if (ma_sound_is_playing(&m_impl->voices[next])) {
            adoptNext(true);
            emit positionChanged(positionMs());
            emit trackFinished();
            return;
        }
        // The scheduled start should already have crossed. If it hasn't
        // within half a second, hand the preloaded sound to loadAndPlay.
        if (!m_boundaryWait.isValid())
            m_boundaryWait.start();
        else if (m_boundaryWait.elapsed() > 500) {
            m_boundaryWait.invalidate();
            m_wasAtEnd = true;
            m_pollTimer.stop();
            emit positionChanged(durationMs());
            emit trackFinished();
            return;
        }
        emit positionChanged(durationMs());
        return;
    }

    m_boundaryWait.invalidate();
    emit positionChanged(positionMs());
    if (atEnd && !m_wasAtEnd) {
        m_wasAtEnd = true;
        m_pollTimer.stop();
        emit trackFinished();
    }
}
