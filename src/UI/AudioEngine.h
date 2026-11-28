#pragma once
#include <QElapsedTimer>
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>

#include "Audio/AudioMixer.h"
#include "Scene/Document.h"
#include "Scene/Playback.h"

#ifdef MYND_HAS_MULTIMEDIA
class QAudioSink;
#endif

namespace mynd {

// Pull-mode device that mixes an AudioScene on demand (what the sound card reads). Thread-safe: the scene can be replaced
// while the audio thread reads. Reads are always full (silence where nothing plays) until `limit` samples were produced.
class AudioMixDevice : public QIODevice {
    Q_OBJECT
public:
    explicit AudioMixDevice(QObject* parent = nullptr);
    void setScene(std::shared_ptr<const AudioScene> s);
    // Next read starts at composition sample `pos`; `limit` < 0 = unlimited, otherwise silence after that many frames.
    void setPosition(qint64 pos, qint64 limit = -1);
    qint64 position() const { return m_pos.load(); }
    bool int16Output = false;                       // convert to 16-bit for devices without float support
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; }
protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char*, qint64) override { return -1; }
private:
    QMutex m_mu;
    std::shared_ptr<const AudioScene> m_scene;
    std::atomic<qint64> m_pos{0};
    std::atomic<qint64> m_limit{-1};
};

// Plays the document's sound in step with Playback (and short snippets while scrubbing). The video clock stays the master:
// when sound and picture drift apart by more than ~120 ms, or the playhead jumps (loop, user seek), the sound restarts at the
// picture's position. Only runs at 100 % speed. Needs Qt Multimedia and an output device; without either it is inert.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    AudioEngine(Playback* playback, QObject* parent = nullptr);
    ~AudioEngine() override;
    void setDocument(Document* d);
    bool available() const { return m_available; }         // an output device exists and the build has Qt Multimedia
    bool playing() const { return m_playing; }
    bool scrubEnabled() const { return m_scrub; }
    void setScrubEnabled(bool on) { m_scrub = on; }
    // Composition sample the sound card is at now (valid while playing()).
    qint64 deviceSample() const;
    AudioMixDevice* device() { return &m_dev; }             // exposed for tests
    // Builds the scene for the current document (also what play() uses).
    std::shared_ptr<const AudioScene> currentScene() const;
    // Test hook: pretend a frame change happened (same path as the document signal).
    void frameShown(int frame) { onFrame(frame); }
    int restarts() const { return m_restarts; }

private:
    void onPlayingChanged(bool p);
    void onFrame(int frame);
    void refreshScene();
    void startAt(int frame);
    void stopSink();
    qint64 sampleOfFrame(int frame) const;
    Playback* m_pb;
    Document* m_doc = nullptr;
    QVector<QMetaObject::Connection> m_conns;      // so a document that is already gone is never touched
    AudioMixDevice m_dev;
#ifdef MYND_HAS_MULTIMEDIA
    QAudioSink* m_sink = nullptr;
#endif
    bool m_available = false, m_playing = false, m_scrub = true;
    qint64 m_baseSample = 0;
    int m_lastFrame = -1, m_restarts = 0;
    QTimer m_scrubStop;
    qint64 m_scrubFrames = 0;
};

}  // namespace mynd
