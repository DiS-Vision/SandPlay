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
    ma_sound sound{};
    ma_device device{};
    ma_biquad_node bands[kBands]{};
    float gains[kBands]{};
    float scratch[2048 * 2]{};
    ma_uint32 channels = 2;
    ma_format playbackFormat = ma_format_f32;
    bool engineReady = false;
    bool soundLoaded = false;
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
    if (m_impl->soundLoaded) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundLoaded = false;
    }
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

bool AudioEngine::loadAndPlay(const QString &path)
{
    if (!m_impl->engineReady) return false;

    if (m_impl->soundLoaded) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundLoaded = false;
        m_loadedPath.clear();
    }

    const QByteArray pathUtf8 = path.toUtf8();
    // MA_SOUND_FLAG_STREAM: decode as playback advances, don't slurp the
    // whole file into RAM up front — this is what keeps memory flat
    // regardless of track length or format.
    ma_result result = ma_sound_init_from_file(
        &m_impl->engine, pathUtf8.constData(),
        MA_SOUND_FLAG_STREAM, nullptr, nullptr, &m_impl->sound);
    if (result != MA_SUCCESS) {
        emit errorOccurred(QStringLiteral("failed to open %1 (%2)").arg(path).arg(int(result)));
        return false;
    }
    m_impl->soundLoaded = true;
    m_loadedPath = path;
    if (m_impl->bandsReady)
        ma_node_attach_output_bus(reinterpret_cast<ma_node *>(&m_impl->sound), 0, &m_impl->bands[0], 0);
    m_wasAtEnd = false;

    ma_sound_start(&m_impl->sound);
    m_pollTimer.start();
    emit playbackStateChanged(true);
    return true;
}

void AudioEngine::pause()
{
    if (!m_impl->soundLoaded) return;
    ma_sound_stop(&m_impl->sound); // miniaudio's "stop" just halts the cursor, doesn't rewind
    m_pollTimer.stop();
    emit playbackStateChanged(false);
}

void AudioEngine::resume()
{
    if (!m_impl->soundLoaded) return;
    ma_sound_start(&m_impl->sound);
    m_pollTimer.start();
    emit playbackStateChanged(true);
}

void AudioEngine::stop()
{
    if (!m_impl->soundLoaded) return;
    ma_sound_stop(&m_impl->sound);
    ma_sound_uninit(&m_impl->sound);
    m_impl->soundLoaded = false;
    m_loadedPath.clear();
    m_pollTimer.stop();
    emit playbackStateChanged(false);
}

void AudioEngine::seekToMs(qint64 ms)
{
    if (!m_impl->soundLoaded) return;
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(&m_impl->sound, nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return;
    const ma_uint64 frame = static_cast<ma_uint64>(ms) * sampleRate / 1000;
    ma_sound_seek_to_pcm_frame(&m_impl->sound, frame);
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
    if (!m_impl->soundLoaded) return 0;
    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(const_cast<ma_sound*>(&m_impl->sound), &cursor);
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(const_cast<ma_sound*>(&m_impl->sound), nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return 0;
    return static_cast<qint64>(cursor) * 1000 / sampleRate;
}

qint64 AudioEngine::durationMs() const
{
    if (!m_impl->soundLoaded) return 0;
    ma_uint64 frames = 0;
    ma_sound_get_length_in_pcm_frames(const_cast<ma_sound*>(&m_impl->sound), &frames);
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(const_cast<ma_sound*>(&m_impl->sound), nullptr, nullptr, &sampleRate, nullptr, 0);
    if (sampleRate == 0) return 0;
    return static_cast<qint64>(frames) * 1000 / sampleRate;
}

bool AudioEngine::isPlaying() const
{
    return m_impl->soundLoaded && ma_sound_is_playing(const_cast<ma_sound*>(&m_impl->sound));
}

void AudioEngine::pollProgress()
{
    if (!m_impl->soundLoaded) return;
    emit positionChanged(positionMs());

    const bool atEnd = ma_sound_at_end(&m_impl->sound);
    if (atEnd && !m_wasAtEnd) {
        m_wasAtEnd = true;
        m_pollTimer.stop();
        emit trackFinished(); // PlaybackQueue::onTrackFinished() picks the next track
    }
}
