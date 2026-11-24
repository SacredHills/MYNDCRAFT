#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "Scene/Document.h"

namespace mynd {

// Real-time playback of a document's timeline. The frame shown is computed from the elapsed wall-clock
// time with exact rational arithmetic (no accumulated rounding), so playback never drifts from the
// frame rate; when the machine cannot keep up, frames are skipped rather than slowing the clock down.
class Playback : public QObject {
    Q_OBJECT
public:
    explicit Playback(QObject* parent = nullptr);
    void setDocument(Document* d);

    bool isPlaying() const { return m_playing; }
    bool loop() const { return m_loop; }
    void setLoop(bool on);
    int speedPercent() const { return m_speedPct; }
    void setSpeedPercent(int pct);            // 10..400
    int skippedFrames() const { return m_skipped; }   // frames not shown during the current/last run

    void play();
    void pause();
    void toggle() { m_playing ? pause() : play(); }
    void stop();                              // pause and return to the first frame of the range

    // Advance to the frame that belongs to `elapsedMicros` after play() (called by the timer; public so
    // tests can drive the clock deterministically).
    void tick(qint64 elapsedMicros);
    // Frame reached after `elapsedMicros` when starting from `startFrame`; sets *finished at the end of a
    // non-looping range. Pure function of its arguments.
    static int frameAt(qint64 elapsedMicros, int startFrame, int first, int last, FrameRate fps, int speedPct,
                       bool loop, bool* finished);

signals:
    void playingChanged(bool playing);
    void loopChanged(bool loop);

private:
    void onTimer();
    Document* m_doc = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_playing = false, m_loop = true;
    int m_speedPct = 100;
    int m_startFrame = 0, m_lastShown = 0, m_skipped = 0;
};

}  // namespace mynd
