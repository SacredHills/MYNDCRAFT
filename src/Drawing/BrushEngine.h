#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>

namespace mynd {

struct BrushSettings {
    double size = 20;          // diameter in px
    double opacity = 1.0;      // stroke-level cap (0..1)
    double flow = 1.0;         // per-dab alpha (0..1)
    double hardness = 0.8;     // 1 = hard edge
    double spacing = 0.1;      // fraction of diameter between dabs
    double stabilization = 0;  // 0..1, lazy-follow smoothing
    bool pressureSize = true;
    bool pressureOpacity = false;
    bool antialias = true;     // false = aliased (pencil)
};

enum class BrushMode { Paint, Erase, Smudge };

// One brush stroke on a raster layer. The layer is updated live: after every addPoint() the layer
// contains (original + stroke buffer * opacity), so the stroke-level opacity cap behaves like
// Photoshop/Clip Studio (overlapping dabs in one stroke do not exceed `opacity`).
class BrushStroke {
public:
    // `layer` must outlive the stroke and keep its size. erase=true removes alpha instead of painting.
    BrushStroke(QImage* layer, const BrushSettings& s, const QColor& color, bool erase);
    // Smudge drags the existing pixels along the stroke; `flow` is the smudge strength.
    BrushStroke(QImage* layer, const BrushSettings& s, const QColor& color, BrushMode mode);

    void begin(const QPointF& p, double pressure = 1.0);
    void addPoint(const QPointF& p, double pressure = 1.0);
    void end();

    QRect dirtyRect() const { return m_dirty; }       // union of everything touched so far
    QRect lastUpdateRect() const { return m_lastUpdate; }
    const QImage& original() const { return m_original; }  // layer contents before the stroke

private:
    void lineTo(const QPointF& p, double pressure);
    void stamp(const QPointF& c, double pressure);
    void smudgeDab(const QPointF& c, double pressure);
    void composite(const QRect& r);

    QImage* m_layer;
    BrushSettings m_s;
    QColor m_color;
    bool m_erase;
    BrushMode m_mode = BrushMode::Paint;
    QImage m_pick;       // smudge: pixels carried by the brush (world coordinates of m_pickBox)
    QRect m_pickBox;
    QPoint m_prevCi;
    bool m_pickValid = false;
    QImage m_original, m_buffer;
    QPointF m_raw, m_smooth, m_last;
    double m_lastPressure = 1, m_rawPressure = 1;
    double m_carry = 0;  // distance since last dab
    bool m_started = false;
    QRect m_dirty, m_lastUpdate, m_pending;
};

}  // namespace mynd
