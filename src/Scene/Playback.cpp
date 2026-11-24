#include "Scene/Playback.h"

#include <algorithm>
#include <numeric>

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
    // frames = elapsed_us * speed% * num / (100 * den * 1e6), kept exact in 64-bit integers (MSVC has no
    // __int128): reduce the fraction, then floor(e*b/c) = (e/c)*b + ((e%c)*b)/c with e%c < c.
    const qint64 e = std::max<qint64>(0, elapsedMicros);
    const qint64 b0 = qint64(speedPct) * fps.num, c0 = qint64(100) * fps.den * 1000000;
    const qint64 g = std::gcd(b0, c0);
    const qint64 b = b0 / g, c = c0 / g;
    qint64 advanced;
    if (c < (qint64(1) << 31) && b < (qint64(1) << 31)) advanced = (e / c) * b + ((e % c) * b) / c;
    else advanced = qint64(static_cast<long double>(e) * b / c);   // unusual rate: tiny rounding risk only here
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
