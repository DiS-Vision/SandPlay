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

} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    ma_sound sound{};
    ma_biquad_node bands[kBands]{};
    float gains[kBands]{};
    ma_uint32 channels = 2;
    bool engineReady = false;
    bool soundLoaded = false;
    bool bandsReady = false;

    void retune(int band)
    {
        if (!bandsReady || band < 0 || band >= kBands) return;
        const auto c = peaking(ma_engine_get_sample_rate(&engine), kBandHz[band], 1.2, gains[band]);
        const ma_biquad_config config = ma_biquad_config_init(
            ma_format_f32, channels, c.b0, c.b1, c.b2, c.a0, c.a1, c.a2);
        ma_biquad_node_reinit(&config, &bands[band]);
    }
};

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    m_pollTimer.setInterval(250); // 4x/sec is plenty smooth for a seek slider
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioEngine::pollProgress);
}

AudioEngine::~AudioEngine()
{
    if (m_impl->soundLoaded) ma_sound_uninit(&m_impl->sound);
    if (m_impl->bandsReady) {
        for (int i = 0; i < kBands; ++i)
            ma_biquad_node_uninit(&m_impl->bands[i], nullptr);
    }
    if (m_impl->engineReady) ma_engine_uninit(&m_impl->engine);
}

bool AudioEngine::init()
{
    ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result != MA_SUCCESS) {
        emit errorOccurred(QStringLiteral("audio device init failed (%1)").arg(int(result)));
        return false;
    }
    m_impl->engineReady = true;
    ma_engine_set_volume(&m_impl->engine, m_volume);

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
    if (made == kBands) {
        for (int i = 0; i < kBands - 1; ++i)
            ma_node_attach_output_bus(&m_impl->bands[i], 0, &m_impl->bands[i + 1], 0);
        ma_node_attach_output_bus(&m_impl->bands[kBands - 1], 0, ma_engine_get_endpoint(&m_impl->engine), 0);
        m_impl->bandsReady = true;
    } else {
        for (int i = 0; i < made; ++i)
            ma_biquad_node_uninit(&m_impl->bands[i], nullptr);
    }
    return true;
}

bool AudioEngine::loadAndPlay(const QString &path)
{
    if (!m_impl->engineReady) return false;

    if (m_impl->soundLoaded) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundLoaded = false;
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
