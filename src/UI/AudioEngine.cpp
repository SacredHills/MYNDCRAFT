#include "UI/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Core/Settings.h"

#ifdef MYND_HAS_MULTIMEDIA
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#endif

namespace mynd {

AudioMixDevice::AudioMixDevice(QObject* parent) : QIODevice(parent) { open(QIODevice::ReadOnly | QIODevice::Unbuffered); }

void AudioMixDevice::setScene(std::shared_ptr<const AudioScene> s) { QMutexLocker l(&m_mu); m_scene = std::move(s); }
void AudioMixDevice::setPosition(qint64 pos, qint64 limit) { m_pos = pos; m_limit = limit; }

qint64 AudioMixDevice::readData(char* data, qint64 maxlen) {
    const int bytesPerFrame = int16Output ? 4 : 8;
    const qint64 frames = maxlen / bytesPerFrame;
    if (frames <= 0) return 0;
    std::shared_ptr<const AudioScene> sc;
    { QMutexLocker l(&m_mu); sc = m_scene; }
    std::vector<float> buf(size_t(frames) * 2, 0.0f);
    const qint64 pos = m_pos.load();
    qint64 live = frames;                                            // frames that may carry sound
    const qint64 lim = m_limit.load();
    if (lim >= 0) live = std::clamp<qint64>(lim, 0, frames);
    if (sc && live > 0) mixAudio(*sc, pos, int(live), buf.data());
    m_pos = pos + frames;
    if (lim >= 0) m_limit = std::max<qint64>(0, lim - frames);
    if (int16Output) {
        auto* o = reinterpret_cast<qint16*>(data);
        for (size_t i = 0; i < buf.size(); ++i) o[i] = qint16(std::lround(std::clamp(buf[i], -1.0f, 1.0f) * 32767.0f));
    } else {
        for (float& v : buf) v = std::clamp(v, -1.0f, 1.0f);          // never hand the device values outside the legal range
        std::memcpy(data, buf.data(), buf.size() * sizeof(float));
    }
    return frames * bytesPerFrame;
}

AudioEngine::AudioEngine(Playback* playback, QObject* parent) : QObject(parent), m_pb(playback) {
    m_scrub = settings::audioScrub();
    m_scrubStop.setSingleShot(true);
    connect(&m_scrubStop, &QTimer::timeout, this, [this] { if (!m_playing) stopSink(); });
#ifdef MYND_HAS_MULTIMEDIA
    const QAudioDevice out = QMediaDevices::defaultAudioOutput();
    if (!out.isNull()) {
        QAudioFormat f;
        f.setSampleRate(kAudioRate); f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Float);
        if (!out.isFormatSupported(f)) { f.setSampleFormat(QAudioFormat::Int16); m_dev.int16Output = true; }
        if (out.isFormatSupported(f)) {
            m_sink = new QAudioSink(out, f, this);
            m_sink->setBufferSize(kAudioRate / 10 * (m_dev.int16Output ? 4 : 8));      // ~100 ms
            m_available = true;
        }
    }
#endif
    connect(m_pb, &Playback::playingChanged, this, &AudioEngine::onPlayingChanged);
}

AudioEngine::~AudioEngine() { stopSink(); }

void AudioEngine::setDocument(Document* d) {
    for (const auto& c : m_conns) disconnect(c);
    m_conns.clear();
    stopSink();
    m_playing = false;
    m_doc = d;
    m_lastFrame = -1;
    if (!d) { m_dev.setScene({}); return; }
    refreshScene();
    m_conns << connect(d, &Document::layerTreeChanged, this, [this] { refreshScene(); })
            << connect(d, &Document::timingChanged, this, [this] { refreshScene(); })
            << connect(d, &Document::tracksChanged, this, [this] { refreshScene(); })
            << connect(d, &Document::frameChanged, this, [this](int f) { onFrame(f); });
}

std::shared_ptr<const AudioScene> AudioEngine::currentScene() const {
    if (!m_doc) return {};
    return std::make_shared<AudioScene>(audioSceneOf(*m_doc->sceneRoot(), m_doc->sceneComp().fps, m_doc->sceneComp().durationFrames));
}
void AudioEngine::refreshScene() { m_dev.setScene(currentScene()); }

qint64 AudioEngine::sampleOfFrame(int frame) const {
    if (!m_doc) return 0;
    const FrameRate r = m_doc->sceneComp().fps;
    return r.isValid() ? qint64(std::llround(double(frame) * r.den / r.num * kAudioRate)) : 0;
}

qint64 AudioEngine::deviceSample() const {
#ifdef MYND_HAS_MULTIMEDIA
    if (m_sink && m_playing) return m_baseSample + qint64(m_sink->processedUSecs()) * kAudioRate / 1000000;
#endif
    return m_dev.position();
}

void AudioEngine::stopSink() {
#ifdef MYND_HAS_MULTIMEDIA
    if (m_sink && m_sink->state() != QAudio::StoppedState) m_sink->stop();
#endif
    m_scrubStop.stop();
}

void AudioEngine::startAt(int frame) {
    m_baseSample = sampleOfFrame(frame);
    m_dev.setPosition(m_baseSample, -1);
#ifdef MYND_HAS_MULTIMEDIA
    if (!m_sink) return;
    if (m_sink->state() != QAudio::StoppedState) m_sink->stop();
    m_sink->start(&m_dev);
#endif
}

void AudioEngine::onPlayingChanged(bool p) {
    m_playing = false;
    if (!m_doc) return;
    if (!p) { stopSink(); return; }
    refreshScene();
    if (!m_available || m_pb->speedPercent() != 100) return;
    const auto sc = currentScene();
    if (!sc || sc->sources.empty()) return;                          // nothing to hear: keep the device closed
    m_playing = true;
    m_lastFrame = m_doc->currentFrame();
    startAt(m_lastFrame);
}

void AudioEngine::onFrame(int frame) {
    if (!m_doc) return;
    const int last = m_lastFrame;
    m_lastFrame = frame;
    if (m_playing) {
        // Picture is the master: re-sync on jumps (loop wrap, seek) and on drift beyond ~120 ms.
        const double drift = double(deviceSample() - sampleOfFrame(frame)) / kAudioRate;
        const bool jumped = frame < last || frame > last + 3;
        if (jumped || std::fabs(drift) > 0.12) { ++m_restarts; startAt(frame); }
        return;
    }
    if (!m_scrub || !m_available || m_pb->isPlaying() || frame == last) return;
    const auto sc = currentScene();
    if (!sc || sc->sources.empty()) return;
    // Scrubbing: a short snippet (one frame, at least 40 ms) so dragging the playhead is audible.
    const FrameRate r = m_doc->sceneComp().fps;
    const double frameSec = r.isValid() ? double(r.den) / r.num : 1.0 / 24;
    m_scrubFrames = qint64(std::max(0.04, std::min(0.12, frameSec)) * kAudioRate);
    m_baseSample = sampleOfFrame(frame);
    m_dev.setPosition(m_baseSample, m_scrubFrames);
#ifdef MYND_HAS_MULTIMEDIA
    if (m_sink) { if (m_sink->state() != QAudio::StoppedState) m_sink->stop(); m_sink->start(&m_dev); }
#endif
    m_scrubStop.start(int(m_scrubFrames * 1000 / kAudioRate) + 250);
}

}  // namespace mynd
