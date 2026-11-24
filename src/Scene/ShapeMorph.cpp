#include "Scene/ShapeMorph.h"

#include <QLineF>
#include <algorithm>

namespace mynd {

namespace {
QPointF mix(const QPointF& a, const QPointF& b, double t) { return a + (b - a) * t; }
QColor mixColor(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
// Split the longest segments until the shape has `target` points.
void padNodes(VectorShape& s, int target) {
    while (s.nodes.size() < target && s.nodes.size() >= 2) {
        const int n = s.nodes.size(), segs = s.closed ? n : n - 1;
        int best = 0; double len = -1;
        for (int i = 0; i < segs; ++i) {
            const double l = QLineF(s.nodes[i].pos, s.nodes[(i + 1) % n].pos).length();
            if (l > len) { len = l; best = i; }
        }
        s.insertNode(best, 0.5);
    }
}
}  // namespace

VectorShape morphShape(const VectorShape& a0, const VectorShape& b0, double t) {
    t = std::clamp(t, 0.0, 1.0);
    VectorShape a = a0, b = b0;
    const int target = std::max(a.nodes.size(), b.nodes.size());
    padNodes(a, target);
    padNodes(b, target);
    VectorShape r = t < 0.5 ? a : b;                 // flags that cannot be blended (closed, caps, gradients) switch halfway
    r.id = a0.id;
    if (a.nodes.size() == b.nodes.size()) {
        for (int i = 0; i < a.nodes.size(); ++i) {
            PathNode& n = r.nodes[i];
            n.pos = mix(a.nodes[i].pos, b.nodes[i].pos, t);
            n.in = mix(a.nodes[i].in, b.nodes[i].in, t);
            n.out = mix(a.nodes[i].out, b.nodes[i].out, t);
        }
    }
    if (a0.fill.type == Paint::Solid && b0.fill.type == Paint::Solid) r.fill.color = mixColor(a0.fill.color, b0.fill.color, t);
    r.stroke.color = mixColor(a0.stroke.color, b0.stroke.color, t);
    r.stroke.width = a0.stroke.width + (b0.stroke.width - a0.stroke.width) * t;
    return r;
}

ShapeList morphShapes(const ShapeList& a, const ShapeList& b, double t) {
    ShapeList out;
    const int n = std::min(a.size(), b.size());
    for (int i = 0; i < n; ++i) out.append(morphShape(a[i], b[i], t));
    if (t < 0.5) for (int i = n; i < a.size(); ++i) out.append(a[i]);
    else for (int i = n; i < b.size(); ++i) out.append(b[i]);
    return out;
}

}  // namespace mynd
