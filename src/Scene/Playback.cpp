#include "Scene/Playback.h"

#include <algorithm>

namespace mynd {

Playback::Playback(QObject* parent) : QObject(parent) {
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Playback::onTimer);
}

void Playback::setDocument(Document* d) {
    if (m_doc == d) return;
    pause();
    m_doc = d;
}

void Playback::setLoop(bool on) {
    if (m_loop == on) return;
    m_loop = on;
    emit loopChanged(on);
}

void Playback::setSpeedPercent(int pct) { m_speedPct = std::clamp(pct, 10, 400); }

int Playback::frameAt(qint64 elapsedMicros, int startFrame, int first, int last, FrameRate fps, int speedPct,
                      bool loop, bool* finished) {
    if (finished) *finished = false;
    const int n = last - first + 1;
    if (n <= 0 || !fps.isValid()) return first;
    // frames = elapsed * speed * fps, in exact integer arithmetic: us * pct * num / (100 * den * 1e6)
    const __int128 num = (__int128)std::max<qint64>(0, elapsedMicros) * speedPct * fps.num;
    const __int128 den = (__int128)100 * fps.den * 1000000;
    const qint64 advanced = qint64(num / den);
    const qint64 start = std::clamp(startFrame, first, last) - first;
    const qint64 pos = start + advanced;
    if (loop) return first + int(pos % n);
    if (pos >= n - 1) { if (finished) *finished = pos >= n; return last; }
    return first + int(pos);
}

void Playback::play() {
    if (m_playing || !m_doc) return;
    const int first = m_doc->rangeStart(), last = m_doc->rangeEnd();
    int f = m_doc->currentFrame();
    if (f < first || f >= last) f = first;          // at (or past) the end: start over
    m_startFrame = f;
    m_lastShown = f;
    m_skipped = 0;
    m_doc->setCurrentFrame(f);
    m_clock.start();
    m_playing = true;
    const FrameRate r = m_doc->comp().fps;
    // Wake up about twice per frame so a frame is never shown more than half a frame late.
    const int ms = r.isValid() ? std::clamp(int(500.0 * r.den / r.num * 100.0 / m_speedPct), 1, 100) : 16;
    m_timer.start(ms);
    emit playingChanged(true);
}

void Playback::pause() {
    if (!m_playing) return;
    m_timer.stop();
    m_playing = false;
    emit playingChanged(false);
}

void Playback::stop() {
    const bool was = m_playing;
    pause();
    if (m_doc) m_doc->setCurrentFrame(m_doc->rangeStart());
    Q_UNUSED(was);
}

void Playback::onTimer() { tick(m_clock.nsecsElapsed() / 1000); }

void Playback::tick(qint64 elapsedMicros) {
    if (!m_playing || !m_doc) return;
    bool finished = false;
    const int f = frameAt(elapsedMicros, m_startFrame, m_doc->rangeStart(), m_doc->rangeEnd(), m_doc->comp().fps,
                          m_speedPct, m_loop, &finished);
    if (f != m_lastShown) {
        // Count frames that were never shown because the clock moved on faster than we drew.
        const int n = m_doc->rangeEnd() - m_doc->rangeStart() + 1;
        int step = f - m_lastShown;
        if (m_loop && step < 0) step += n;
        if (step > 1) m_skipped += step - 1;
        m_lastShown = f;
        m_doc->setCurrentFrame(f);
    }
    if (finished) pause();
}

}  // namespace mynd
