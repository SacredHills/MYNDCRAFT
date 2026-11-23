#include "Drawing/BrushEngine.h"

#include <algorithm>
#include <cmath>

namespace mynd {

BrushStroke::BrushStroke(QImage* layer, const BrushSettings& s, const QColor& color, BrushMode mode)
    : BrushStroke(layer, s, color, mode == BrushMode::Erase) {
    m_mode = mode;
}

BrushStroke::BrushStroke(QImage* layer, const BrushSettings& s, const QColor& color, bool erase)
    : m_layer(layer), m_s(s), m_color(color), m_erase(erase) {
    m_mode = erase ? BrushMode::Erase : BrushMode::Paint;
    m_original = layer->copy();
    m_buffer = QImage(layer->size(), QImage::Format_ARGB32_Premultiplied);
    m_buffer.fill(Qt::transparent);
    m_s.size = std::max(0.5, m_s.size);
    m_s.spacing = std::clamp(m_s.spacing, 0.01, 2.0);
    m_s.flow = std::clamp(m_s.flow, 0.0, 1.0);
    m_s.opacity = std::clamp(m_s.opacity, 0.0, 1.0);
    m_s.hardness = std::clamp(m_s.hardness, 0.0, 1.0);
    m_s.stabilization = std::clamp(m_s.stabilization, 0.0, 1.0);
}

void BrushStroke::begin(const QPointF& p, double pressure) {
    m_started = true;
    m_raw = m_smooth = m_last = p;
    m_rawPressure = m_lastPressure = pressure;
    m_carry = 0;
    m_pending = QRect();
    stamp(p, pressure);
    composite(m_pending);
    m_lastUpdate = m_pending;
}

void BrushStroke::addPoint(const QPointF& p, double pressure) {
    if (!m_started) { begin(p, pressure); return; }
    m_raw = p;
    m_rawPressure = pressure;
    // Lazy-follow stabilization: the brush trails the cursor by a fraction of the distance.
    const double k = 1.0 - 0.96 * m_s.stabilization;
    const QPointF target = m_smooth + (p - m_smooth) * k;
    m_pending = QRect();
    lineTo(target, pressure);
    m_smooth = target;
    composite(m_pending);
    m_lastUpdate = m_pending;
}

void BrushStroke::end() {
    if (!m_started) return;
    // Flush: with stabilization the brush lags; let it arrive at the final cursor position.
    m_pending = QRect();
    if (m_s.stabilization > 0) {
        for (int i = 0; i < 400 && QLineF(m_smooth, m_raw).length() > 0.25; ++i) {
            const double k = 1.0 - 0.96 * m_s.stabilization;
            QPointF t = m_smooth + (m_raw - m_smooth) * k;
            lineTo(t, m_rawPressure);
            m_smooth = t;
        }
    }
    composite(m_pending);
    m_lastUpdate = m_pending;
    m_started = false;
}

void BrushStroke::lineTo(const QPointF& p, double pressure) {
    const double dist = QLineF(m_last, p).length();
    if (dist <= 0) return;
    double pos = 0;
    // Spacing depends on the (pressure-scaled) diameter at the current position.
    while (true) {
        const double t0 = pos / dist;
        const double pr = m_lastPressure + (pressure - m_lastPressure) * t0;
        const double diam = m_s.size * (m_s.pressureSize ? std::max(0.05, pr) : 1.0);
        const double step = std::max(0.5, diam * m_s.spacing);
        const double need = step - m_carry;
        if (pos + need > dist) { m_carry += dist - pos; break; }
        pos += need;
        m_carry = 0;
        const double t = pos / dist;
        stamp(m_last + (p - m_last) * t, m_lastPressure + (pressure - m_lastPressure) * t);
    }
    m_last = p;
    m_lastPressure = pressure;
}

void BrushStroke::stamp(const QPointF& c, double pressure) {
    if (m_mode == BrushMode::Smudge) { smudgeDab(c, pressure); return; }
    const double diam = m_s.size * (m_s.pressureSize ? std::max(0.05, pressure) : 1.0);
    const double r = diam / 2.0;
    const double flow = m_s.flow * (m_s.pressureOpacity ? std::clamp(pressure, 0.0, 1.0) : 1.0);
    const double inner = r * m_s.hardness;
    const QRect box = QRect(int(std::floor(c.x() - r - 1)), int(std::floor(c.y() - r - 1)),
                            int(std::ceil(diam)) + 3, int(std::ceil(diam)) + 3)
                          .intersected(m_buffer.rect());
    if (box.isEmpty()) return;

    // Premultiplied source color components (alpha applied per-pixel below).
    const float cr = m_color.redF(), cg = m_color.greenF(), cb = m_color.blueF();
    const float colorAlpha = m_erase ? 1.0f : float(m_color.alphaF());

    for (int y = box.top(); y <= box.bottom(); ++y) {
        auto* row = reinterpret_cast<quint32*>(m_buffer.scanLine(y));
        for (int x = box.left(); x <= box.right(); ++x) {
            const double dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
            const double d = std::sqrt(dx * dx + dy * dy);
            double cov;
            if (!m_s.antialias) {
                cov = d <= r ? 1.0 : 0.0;
                if (cov > 0 && m_s.hardness < 1.0) {
                    const double f = d <= inner ? 1.0 : std::clamp((r - d) / std::max(1e-6, r - inner), 0.0, 1.0);
                    cov = f;
                }
            } else {
                const double edge = std::clamp(r - d + 0.5, 0.0, 1.0);  // 1px antialiased rim
                double f = 1.0;
                if (d > inner && r > inner) {
                    f = std::clamp((r - d) / (r - inner), 0.0, 1.0);
                    f = f * f * (3 - 2 * f);  // smoothstep falloff
                }
                cov = edge * f;
            }
            if (cov <= 0) continue;
            const float sa = float(cov * flow) * colorAlpha;
            if (sa <= 0) continue;
            const quint32 dst = row[x];
            const float da = float(dst >> 24) / 255.0f;
            const float dr = float((dst >> 16) & 0xff) / 255.0f;
            const float dg = float((dst >> 8) & 0xff) / 255.0f;
            const float db = float(dst & 0xff) / 255.0f;
            const float oa = sa + da * (1 - sa);
            const float orr = cr * sa + dr * (1 - sa);
            const float og = cg * sa + dg * (1 - sa);
            const float ob = cb * sa + db * (1 - sa);
            auto q = [](float v) { return quint32(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            row[x] = (q(oa) << 24) | (std::min(q(orr), q(oa)) << 16) | (std::min(q(og), q(oa)) << 8) | std::min(q(ob), q(oa));
        }
    }
    m_pending = m_pending.isNull() ? box : m_pending.united(box);
    m_dirty = m_dirty.isNull() ? box : m_dirty.united(box);
}

void BrushStroke::composite(const QRect& rect) {
    if (m_mode == BrushMode::Smudge) return;  // smudge edits the layer directly
    const QRect r = rect.intersected(m_layer->rect());
    if (r.isEmpty()) return;
    const float op = float(m_s.opacity);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const auto* orow = reinterpret_cast<const quint32*>(m_original.constScanLine(y));
        const auto* brow = reinterpret_cast<const quint32*>(m_buffer.constScanLine(y));
        auto* lrow = reinterpret_cast<quint32*>(m_layer->scanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const quint32 b = brow[x];
            if ((b >> 24) == 0) { lrow[x] = orow[x]; continue; }
            const quint32 o = orow[x];
            const float ba = float(b >> 24) / 255.0f * op;
            if (m_erase) {
                const float k = 1.0f - ba;
                auto ch = [&](int sh) { return quint32(float((o >> sh) & 0xff) * k + 0.5f); };
                lrow[x] = (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
            } else {
                const float k = 1.0f - ba;
                auto ch = [&](int sh) {
                    const float s = float((b >> sh) & 0xff) * op;
                    return quint32(std::min(255.0f, s + float((o >> sh) & 0xff) * k + 0.5f));
                };
                const quint32 a = ch(24);
                lrow[x] = (a << 24) | (std::min(ch(16), a) << 16) | (std::min(ch(8), a) << 8) | std::min(ch(0), a);
            }
        }
    }
}

void BrushStroke::smudgeDab(const QPointF& c, double pressure) {
    const double diam = m_s.size * (m_s.pressureSize ? std::max(0.05, pressure) : 1.0);
    const double r = diam / 2.0, inner = r * m_s.hardness;
    const QRect box = QRect(int(std::floor(c.x() - r - 1)), int(std::floor(c.y() - r - 1)),
                            int(std::ceil(diam)) + 3, int(std::ceil(diam)) + 3).intersected(m_layer->rect());
    if (box.isEmpty()) return;
    const QPoint ci(int(std::lround(c.x())), int(std::lround(c.y())));
    if (!m_pickValid) {                // first dab only picks up what is under the brush
        m_pick = m_layer->copy(box);
        m_pickBox = box;
        m_prevCi = ci;
        m_pickValid = true;
        m_pending = m_pending.isNull() ? box : m_pending.united(box);
        m_dirty = m_dirty.isNull() ? box : m_dirty.united(box);
        return;
    }
    const QPoint shift = ci - m_prevCi;
    QImage next(box.size(), QImage::Format_ARGB32_Premultiplied);
    const float strength = float(std::clamp(m_s.flow, 0.0, 1.0));
    for (int y = box.top(); y <= box.bottom(); ++y) {
        auto* lrow = reinterpret_cast<quint32*>(m_layer->scanLine(y));
        auto* nrow = reinterpret_cast<quint32*>(next.scanLine(y - box.top()));
        for (int x = box.left(); x <= box.right(); ++x) {
            const double dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
            const double d = std::sqrt(dx * dx + dy * dy);
            double f = 1.0;
            if (d > inner && r > inner) { f = std::clamp((r - d) / (r - inner), 0.0, 1.0); f = f * f * (3 - 2 * f); }
            const double cov = std::clamp(r - d + 0.5, 0.0, 1.0) * f;
            const quint32 dst = lrow[x];
            const QPoint sp(x - shift.x(), y - shift.y());   // where this pixel's carried paint came from
            const quint32 src = m_pickBox.contains(sp)
                                    ? reinterpret_cast<const quint32*>(m_pick.constScanLine(sp.y() - m_pickBox.top()))[sp.x() - m_pickBox.left()]
                                    : dst;
            const float k = float(cov) * strength;
            quint32 out = dst;
            if (k > 0) {
                auto ch = [&](int sh) { return quint32(float((dst >> sh) & 0xff) * (1 - k) + float((src >> sh) & 0xff) * k + 0.5f); };
                const quint32 a = ch(24);
                out = (a << 24) | (std::min(ch(16), a) << 16) | (std::min(ch(8), a) << 8) | std::min(ch(0), a);
                lrow[x] = out;
            }
            nrow[x - box.left()] = out;
        }
    }
    m_pick = next;
    m_pickBox = box;
    m_prevCi = ci;
    m_pending = m_pending.isNull() ? box : m_pending.united(box);
    m_dirty = m_dirty.isNull() ? box : m_dirty.united(box);
}

}  // namespace mynd
