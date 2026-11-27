#include "UI/Viewport3D.h"

#include "Graphics/Compositor.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <functional>

namespace mynd {

namespace {

// The scene as it looks at the current frame (animated properties overlaid): what the viewport shows, picks and attaches gizmos to.
std::shared_ptr<const Scene3D> shownScene(const Layer& l, const Document* d) { return l.sceneAt(d ? d->currentFrame() : 0); }
const QVector3D kAxis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const QColor kAxisColor[3] = {QColor(235, 70, 70), QColor(90, 200, 90), QColor(70, 120, 240)};

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + ab * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

// Closest point parameter on line (o0 + a*t) to the ray (ro + rd*s).
bool lineParam(const QVector3D& o0, const QVector3D& a, const Ray& ray, double* t) {
    const QVector3D w0 = o0 - ray.origin;
    const double b = QVector3D::dotProduct(a, ray.dir), d = QVector3D::dotProduct(a, w0), e = QVector3D::dotProduct(ray.dir, w0);
    const double denom = 1.0 - b * b;
    if (std::abs(denom) < 1e-5) return false;
    *t = (b * e - d) / denom;
    return true;
}

QMatrix4x4 withoutTranslation(QMatrix4x4 m) { m.setColumn(3, QVector4D(0, 0, 0, 1)); return m; }

}  // namespace

class ViewCanvas : public QWidget {
public:
    explicit ViewCanvas(Viewport3D* v) : QWidget(v), m_v(v) {
        setMouseTracking(false);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(160, 120);
    }
    QRect viewRect() const {
        const Document* d = m_v->m_s->doc();
        if (!m_v->m_cameraView || !d || d->canvasSize().isEmpty()) return rect();
        const double a = double(d->canvasSize().width()) / d->canvasSize().height();
        int w = width(), h = int(w / a);
        if (h > height()) { h = height(); w = int(h * a); }
        return QRect((width() - w) / 2, (height() - h) / 2, std::max(1, w), std::max(1, h));
    }
    RenderCamera camera(const Scene3D& sc) const {
        const QRect r = viewRect();
        const double aspect = double(r.width()) / std::max(1, r.height());
        if (m_v->m_cameraView) return sceneCamera(sc, aspect);
        return m_v->orbit().toCamera(aspect);
    }

    // ---- gizmo geometry ----
    bool selectedWorld(QMatrix4x4* w) const {
        const Layer* l = m_v->m_s->active3DLayer();
        if (!l) return false;
        const auto sc = shownScene(*l, m_v->m_s->doc());
        if (!sc->node(m_v->m_s->selectedNode())) return false;
        *w = sc->worldMatrix(m_v->m_s->selectedNode());
        return true;
    }
    double gizmoLength(const RenderCamera& cam, const QVector3D& o) const {
        const double dist = std::max(1e-3, double((o - cam.eye).length()));
        return dist * 0.18;
    }
    QVector3D axisDir(int i, const QMatrix4x4& world) const {
        if (m_v->m_mode != Viewport3D::Mode::Scale) return kAxis[i];
        QVector3D d = world.mapVector(kAxis[i]);                 // scaling acts on the node's own axes
        return d.isNull() ? kAxis[i] : d.normalized();
    }
    Viewport3D::Handle hitHandle(const QPointF& p, const RenderCamera& cam, const QMatrix4x4& world) const {
        const QRect vr = viewRect();
        const QSize sz = vr.size();
        const QPointF lp = p - vr.topLeft();
        const QVector3D o = world.map(QVector3D(0, 0, 0));
        const double L = gizmoLength(cam, o);
        QPointF so;
        if (!projectToScreen(cam, o, sz, &so)) return Viewport3D::Handle::None;
        double best = 8; Viewport3D::Handle hit = Viewport3D::Handle::None;
        if (m_v->m_mode == Viewport3D::Mode::Scale && std::hypot(lp.x() - so.x(), lp.y() - so.y()) < 9) return Viewport3D::Handle::Center;
        for (int i = 0; i < 3; ++i) {
            const QVector3D a = axisDir(i, world);
            double d = 1e9;
            if (m_v->m_mode == Viewport3D::Mode::Rotate) {
                QVector3D u, v;
                planeBasis(a, &u, &v);
                QPointF prev; bool havePrev = false;
                for (int k = 0; k <= 64; ++k) {
                    const double t = k * 2 * M_PI / 64;
                    QPointF q;
                    if (!projectToScreen(cam, o + (u * float(std::cos(t)) + v * float(std::sin(t))) * float(L * 0.9), sz, &q)) { havePrev = false; continue; }
                    if (havePrev) d = std::min(d, distToSegment(lp, prev, q));
                    prev = q; havePrev = true;
                }
            } else {
                QPointF se;
                if (!projectToScreen(cam, o + a * float(L), sz, &se)) continue;
                d = distToSegment(lp, so, se);
            }
            if (d < best) { best = d; hit = Viewport3D::Handle(int(Viewport3D::Handle::X) + i); }
        }
        return hit;
    }
    static void planeBasis(const QVector3D& n, QVector3D* u, QVector3D* v) {
        const QVector3D t = std::abs(n.y()) < 0.9f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
        *u = QVector3D::crossProduct(n, t).normalized();
        *v = QVector3D::crossProduct(n, *u).normalized();
    }

    // ---- painting ----
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(38, 41, 46));
        const Layer* l = m_v->m_s->active3DLayer();
        if (!l) {
            p.setPen(QColor(170, 170, 170));
            p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                       tr("Select a 3D scene layer, or create one from the 3D menu (New 3D Scene Layer / Import 3D Model…)."));
            return;
        }
        const auto shown = shownScene(*l, m_v->m_s->doc());
        const Scene3D& sc = *shown;
        const QRect vr = viewRect();
        const RenderCamera cam = camera(sc);
        p.setClipRect(vr);
        p.fillRect(vr, QColor(52, 56, 62));
        p.translate(vr.topLeft());
        drawGrid(p, cam, vr.size());
        RenderOptions ro; ro.supersample = 1;
        {
            // cards show their 2D layer's current content, so the viewport draws the same scene the composition does
            const auto drawn = Compositor::sceneForDrawing(*l, m_v->m_s->doc()->currentFrame(), m_v->m_s->doc()->canvasSize(), m_v->m_s->doc());
            p.drawImage(0, 0, renderScene(*drawn, cam, vr.size(), ro));
        }
        drawMarkers(p, sc, cam, vr.size());
        drawSelection(p, sc, cam, vr.size());
        p.resetTransform();
        p.setClipping(false);
        if (m_v->m_cameraView) { p.setPen(QPen(QColor(255, 200, 60), 1)); p.setBrush(Qt::NoBrush); p.drawRect(vr.adjusted(0, 0, -1, -1)); }
        p.setPen(QColor(200, 200, 200));
        p.drawText(8, 16, m_v->m_cameraView ? tr("Scene camera view") : tr("Perspective   (Alt+drag orbit · Shift+Alt+drag pan · wheel zoom · F frame)"));
    }
    void drawGrid(QPainter& p, const RenderCamera& cam, const QSize& sz) {
        const double dist = std::max(0.1, m_v->m_cameraView ? double(cam.eye.length()) + 1 : m_v->orbit().distance);
        const double step = std::pow(10.0, std::floor(std::log10(dist / 3.0)));
        const int n = 20;
        auto line = [&](QVector3D a, QVector3D b, const QPen& pen) {
            QPointF pa, pb;
            if (!projectToScreen(cam, a, sz, &pa) || !projectToScreen(cam, b, sz, &pb)) return;
            p.setPen(pen); p.drawLine(pa, pb);
        };
        const QPen minor(QColor(255, 255, 255, 22), 1), major(QColor(255, 255, 255, 50), 1);
        for (int i = -n; i <= n; ++i) {
            const QPen& pen = (i % 5 == 0) ? major : minor;
            line(QVector3D(float(i * step), 0, float(-n * step)), QVector3D(float(i * step), 0, float(n * step)), i == 0 ? QPen(kAxisColor[2], 1) : pen);
            line(QVector3D(float(-n * step), 0, float(i * step)), QVector3D(float(n * step), 0, float(i * step)), i == 0 ? QPen(kAxisColor[0], 1) : pen);
        }
    }
    void drawMarkers(QPainter& p, const Scene3D& sc, const RenderCamera& cam, const QSize& sz) {
        for (const auto& n : sc.nodes) {
            if ((n.kind != NodeKind::Light && n.kind != NodeKind::Camera) || !sc.effectivelyVisible(n.id)) continue;
            const QMatrix4x4 w = sc.worldMatrix(n.id);
            const QVector3D o = w.map(QVector3D(0, 0, 0));
            QPointF so;
            if (!projectToScreen(cam, o, sz, &so)) continue;
            const double L = gizmoLength(cam, o) * 0.6;
            const bool sel = n.id == m_v->m_s->selectedNode();
            p.setPen(QPen(sel ? QColor(255, 170, 40) : QColor(240, 220, 120), 1.5));
            p.setBrush(Qt::NoBrush);
            if (n.kind == NodeKind::Light) {
                p.drawEllipse(so, 6, 6);
                for (int k = 0; k < 8; ++k) { const double a = k * M_PI / 4; p.drawLine(so + QPointF(std::cos(a) * 8, std::sin(a) * 8), so + QPointF(std::cos(a) * 11, std::sin(a) * 11)); }
                if (n.lightType != LightType::Point) {
                    QPointF se;
                    if (projectToScreen(cam, o + w.mapVector(QVector3D(0, 0, -1)).normalized() * float(L * 2), sz, &se)) p.drawLine(so, se);
                }
            } else {
                const double hh = std::tan(n.fov * M_PI / 360.0) * L, hw = hh * 1.6;
                const QVector3D corners[4] = {{float(-hw), float(-hh), float(-L)}, {float(hw), float(-hh), float(-L)}, {float(hw), float(hh), float(-L)}, {float(-hw), float(hh), float(-L)}};
                QPointF pc[4]; bool ok = true;
                for (int k = 0; k < 4; ++k) ok &= projectToScreen(cam, w.map(corners[k]), sz, &pc[k]);
                if (ok) { for (int k = 0; k < 4; ++k) { p.drawLine(so, pc[k]); p.drawLine(pc[k], pc[(k + 1) % 4]); } }
                if (n.id == sc.activeCamera) { p.setBrush(QColor(255, 200, 60)); p.drawEllipse(so, 3, 3); }
            }
        }
    }
    void drawSelection(QPainter& p, const Scene3D& sc, const RenderCamera& cam, const QSize& sz) {
        const QUuid id = m_v->m_s->selectedNode();
        const Node3D* n = sc.node(id);
        if (!n) return;
        // Bounding box of the node's meshes (including descendants), measured in the node's OWN frame and drawn through its world matrix,
        // so it turns and moves with the object instead of growing into a larger axis-aligned box. Skinned (animated) meshes are measured
        // from their deformed vertices so the box follows the pose that is on screen.
        const QMatrix4x4 selWorld = sc.worldMatrix(id);
        bool invertible = false;
        const QMatrix4x4 toLocal = selWorld.inverted(&invertible);
        QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f); bool any = false;
        auto grow = [&](const QVector3D& q) {
            lo = QVector3D(std::min(lo.x(), q.x()), std::min(lo.y(), q.y()), std::min(lo.z(), q.z()));
            hi = QVector3D(std::max(hi.x(), q.x()), std::max(hi.y(), q.y()), std::max(hi.z(), q.z()));
            any = true;
        };
        std::function<void(const QUuid&, int)> walk = [&](const QUuid& i, int depth) {
            if (depth > 128) return;
            const Node3D* c = sc.node(i);
            if (c && c->kind == NodeKind::Mesh) {
                auto it = sc.meshes.find(c->mesh);
                if (it != sc.meshes.end()) {
                    std::shared_ptr<const Mesh> skinned;
                    if (it->second->positions.size() <= 400000) skinned = skinnedMeshFor(sc, *c);       // (huge skinned meshes: rest-pose box)
                    if (skinned) {
                        for (const QVector3D& v : skinned->positions) grow(invertible ? toLocal.map(v) : v);
                    } else {
                        QVector3D a, b; it->second->bounds(&a, &b);
                        const QMatrix4x4 m = invertible ? toLocal * sc.worldMatrix(i) : sc.worldMatrix(i);
                        for (int k = 0; k < 8; ++k) grow(m.map(QVector3D(k & 1 ? b.x() : a.x(), k & 2 ? b.y() : a.y(), k & 4 ? b.z() : a.z())));
                    }
                }
            }
            for (const QUuid& ch : sc.childrenOf(i)) walk(ch, depth + 1);
        };
        walk(id, 0);
        if (any) {
            p.setPen(QPen(QColor(255, 170, 40), 1, Qt::DashLine));
            const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            auto corner = [&](int k) {
                const QVector3D l(k & 1 ? hi.x() : lo.x(), k & 2 ? hi.y() : lo.y(), k & 4 ? hi.z() : lo.z());
                return invertible ? selWorld.map(l) : l;
            };
            for (auto& e : edges) {
                QPointF a, b;
                if (projectToScreen(cam, corner(e[0]), sz, &a) && projectToScreen(cam, corner(e[1]), sz, &b)) p.drawLine(a, b);
            }
        }
        // gizmo
        const QMatrix4x4 world = sc.worldMatrix(id);
        const QVector3D o = world.map(QVector3D(0, 0, 0));
        QPointF so;
        if (!projectToScreen(cam, o, sz, &so)) return;
        const double L = gizmoLength(cam, o);
        for (int i = 0; i < 3; ++i) {
            const bool active = m_dragHandle == int(Viewport3D::Handle::X) + i;
            QPen pen(active ? QColor(255, 240, 80) : kAxisColor[i], active ? 3 : 2);
            p.setPen(pen); p.setBrush(pen.color());
            const QVector3D a = axisDir(i, world);
            if (m_v->m_mode == Viewport3D::Mode::Rotate) {
                QVector3D u, v; planeBasis(a, &u, &v);
                QPolygonF poly;
                for (int k = 0; k <= 64; ++k) { const double t = k * 2 * M_PI / 64; QPointF q; if (projectToScreen(cam, o + (u * float(std::cos(t)) + v * float(std::sin(t))) * float(L * 0.9), sz, &q)) poly << q; }
                p.setBrush(Qt::NoBrush); p.drawPolyline(poly);
            } else {
                QPointF se;
                if (!projectToScreen(cam, o + a * float(L), sz, &se)) continue;
                p.drawLine(so, se);
                if (m_v->m_mode == Viewport3D::Mode::Scale) p.drawRect(QRectF(se.x() - 4, se.y() - 4, 8, 8));
                else { p.drawEllipse(se, 4.5, 4.5); }
            }
        }
        if (m_v->m_mode == Viewport3D::Mode::Scale) { p.setPen(QPen(QColor(230, 230, 230), 1.5)); p.setBrush(QColor(230, 230, 230, 120)); p.drawRect(QRectF(so.x() - 5, so.y() - 5, 10, 10)); }
    }

    // ---- interaction ----
    enum class Drag { None, Orbit, Pan, Gizmo } m_drag = Drag::None;
    QPoint m_last;
    int m_dragHandle = 0;
    int m_gesture = 0;
    QMatrix4x4 m_startWorld, m_parentWorld;
    QVector3D m_startPos, m_startRot, m_startScale, m_axis, m_origin;
    double m_t0 = 0, m_a0 = 0, m_L0 = 1;
    QPoint m_press;
    QString m_key;

    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        m_last = e->pos(); m_press = e->pos();
        Layer* l = m_v->m_s->active3DLayer();
        if (!l) return;
        const bool alt = e->modifiers() & Qt::AltModifier, shift = e->modifiers() & Qt::ShiftModifier;
        if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && alt)) {
            if (m_v->m_cameraView) return;
            m_drag = (shift || (e->button() == Qt::MiddleButton && shift)) ? Drag::Pan : Drag::Orbit;
            if (e->button() == Qt::MiddleButton && shift) m_drag = Drag::Pan;
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        const auto shown = shownScene(*l, m_v->m_s->doc());
        const Scene3D& sc = *shown;
        const RenderCamera cam = camera(sc);
        const QRect vr = viewRect();
        QMatrix4x4 world;
        if (selectedWorld(&world) && !l->props.locked) {
            const Viewport3D::Handle h = hitHandle(e->position(), cam, world);
            if (h != Viewport3D::Handle::None) { beginGizmo(h, e->position(), cam, sc, world); return; }
        }
        const PickHit hit = pickNode(sc, cam, vr.size(), e->position() - vr.topLeft());
        m_v->m_s->setSelectedNode(hit.node);
        update();
    }

    void beginGizmo(Viewport3D::Handle h, const QPointF& pos, const RenderCamera& cam, const Scene3D& sc, const QMatrix4x4& world) {
        const Node3D* n = sc.node(m_v->m_s->selectedNode());
        m_drag = Drag::Gizmo; m_dragHandle = int(h); m_gesture++;
        m_key = QString("gizmo:%1").arg(m_gesture);
        m_startWorld = world; m_origin = world.map(QVector3D(0, 0, 0));
        m_parentWorld = n->parent.isNull() ? QMatrix4x4() : sc.worldMatrix(n->parent);
        m_startPos = n->position; m_startRot = n->rotation; m_startScale = n->scale;
        m_L0 = gizmoLength(cam, m_origin);
        const QRect vr = viewRect();
        const Ray ray = screenRay(cam, vr.size(), pos - vr.topLeft());
        if (h != Viewport3D::Handle::Center) {
            m_axis = axisDir(int(h) - int(Viewport3D::Handle::X), world);
            if (m_v->m_mode == Viewport3D::Mode::Rotate) m_a0 = ringAngle(ray);
            else if (!lineParam(m_origin, m_axis, ray, &m_t0)) m_t0 = 0;
        }
        update();
    }
    double ringAngle(const Ray& ray) const {
        const double den = QVector3D::dotProduct(ray.dir, m_axis);
        if (std::abs(den) < 1e-5) return m_a0;
        const double s = QVector3D::dotProduct(m_origin - ray.origin, m_axis) / den;
        const QVector3D v = ray.origin + ray.dir * float(s) - m_origin;
        QVector3D u, w; planeBasis(m_axis, &u, &w);
        return std::atan2(QVector3D::dotProduct(v, w), QVector3D::dotProduct(v, u));
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        Layer* l = m_v->m_s->active3DLayer();
        if (!l) return;
        const QPoint d = e->pos() - m_last;
        if (m_drag == Drag::Orbit) {
            OrbitCamera& oc = m_v->orbit();
            oc.yawDeg -= d.x() * 0.4; oc.pitchDeg = std::clamp(oc.pitchDeg + d.y() * 0.4, -89.0, 89.0);
            update();
        } else if (m_drag == Drag::Pan) {
            OrbitCamera& oc = m_v->orbit();
            const RenderCamera cam = oc.toCamera(double(width()) / std::max(1, height()));
            const QMatrix4x4 inv = cam.view.inverted();
            const QVector3D right = inv.mapVector(QVector3D(1, 0, 0)).normalized(), up = inv.mapVector(QVector3D(0, 1, 0)).normalized();
            const double k = oc.distance * 2 * std::tan(oc.fov * M_PI / 360.0) / std::max(1, height());
            oc.target += (-right * float(d.x()) + up * float(d.y())) * float(k);
            update();
        } else if (m_drag == Drag::Gizmo && !l->props.locked) {
            dragGizmo(*l, e->position(), e->modifiers());
        }
        m_last = e->pos();
    }

    void dragGizmo(Layer& l, const QPointF& pos, Qt::KeyboardModifiers mods) {
        const auto shown = shownScene(l, m_v->m_s->doc());
        const Scene3D& sc = *shown;
        const QUuid id = m_v->m_s->selectedNode();
        if (!sc.node(id)) return;
        const RenderCamera cam = camera(sc);
        const QRect vr = viewRect();
        const Ray ray = screenRay(cam, vr.size(), pos - vr.topLeft());
        const Viewport3D::Handle h = Viewport3D::Handle(m_dragHandle);
        const bool snap = mods & Qt::ControlModifier;
        std::function<bool(Scene3D&)> fn;
        const QMatrix4x4 pinv = m_parentWorld.inverted();
        switch (m_v->m_mode) {
        case Viewport3D::Mode::Move: {
            double t;
            if (h == Viewport3D::Handle::Center || !lineParam(m_origin, m_axis, ray, &t)) return;
            double delta = t - m_t0;
            if (snap) delta = std::round(delta / 0.25) * 0.25;
            const QVector3D np = pinv.map(m_origin + m_axis * float(delta));
            fn = [id, np](Scene3D& s) { Node3D* n = s.node(id); if (!n || n->position == np) return false; n->position = np; return true; };
            break;
        }
        case Viewport3D::Mode::Rotate: {
            if (h == Viewport3D::Handle::Center) return;
            double delta = ringAngle(ray) - m_a0;
            if (snap) delta = std::round(delta / (M_PI / 12)) * (M_PI / 12);
            QMatrix4x4 R; R.rotate(float(delta * 180 / M_PI), m_axis);
            QMatrix4x4 nw; nw.translate(m_origin); nw = nw * R * withoutTranslation(m_startWorld);
            const QMatrix4x4 local = pinv * nw;
            QVector3D t, e, sck; decomposeMatrix(local, &t, &e, &sck);
            // the same orientation, written as the Euler angles nearest to the current ones (keyframes then interpolate the short way)
            fn = [id, e0 = e](Scene3D& s) { Node3D* n = s.node(id); if (!n) return false; const QVector3D e = eulerNear(e0, n->rotation); if (n->rotation == e) return false; n->rotation = e; return true; };
            break;
        }
        case Viewport3D::Mode::Scale: {
            QVector3D ns = m_startScale;
            if (h == Viewport3D::Handle::Center) {
                const double f = std::exp((pos.x() - m_press.x()) / 120.0);
                ns = m_startScale * float(f);
            } else {
                double t;
                if (!lineParam(m_origin, m_axis, ray, &t) || std::abs(m_t0) < m_L0 * 0.05) return;
                const double f = std::clamp(t / m_t0, -50.0, 50.0);
                const int ax = int(h) - int(Viewport3D::Handle::X);
                ns[ax] = float(m_startScale[ax] * f);
            }
            if (snap) for (int i = 0; i < 3; ++i) ns[i] = float(std::round(ns[i] / 0.1) * 0.1);
            for (int i = 0; i < 3; ++i) if (std::abs(ns[i]) < 1e-4f) ns[i] = ns[i] < 0 ? -1e-4f : 1e-4f;
            fn = [id, ns](Scene3D& s) { Node3D* n = s.node(id); if (!n || n->scale == ns) return false; n->scale = ns; return true; };
            break;
        }
        }
        m_v->m_s->doc()->editScene3D(l.id, fn, tr("Transform Node"), m_key, m_v->m_s->autoKey());
    }

    void mouseReleaseEvent(QMouseEvent*) override { m_drag = Drag::None; m_dragHandle = 0; update(); }
    void wheelEvent(QWheelEvent* e) override {
        if (!m_v->m_s->active3DLayer() || m_v->m_cameraView) return;
        OrbitCamera& oc = m_v->orbit();
        oc.distance = std::clamp(oc.distance * std::pow(0.9, e->angleDelta().y() / 120.0), 0.01, 1e6);
        update();
    }
    void keyPressEvent(QKeyEvent* e) override {
        switch (e->key()) {
        case Qt::Key_W: m_v->setMode(Viewport3D::Mode::Move); break;
        case Qt::Key_E: m_v->setMode(Viewport3D::Mode::Rotate); break;
        case Qt::Key_R: m_v->setMode(Viewport3D::Mode::Scale); break;
        case Qt::Key_F: m_v->frame(!m_v->m_s->selectedNode().isNull()); break;
        case Qt::Key_Delete: case Qt::Key_Backspace: m_v->m_a->deleteSelected(); break;
        default: QWidget::keyPressEvent(e);
        }
    }

private:
    Viewport3D* m_v;
};

Viewport3D::Viewport3D(EditorSession* s, Scene3DActions* a, QWidget* parent) : QWidget(parent), m_s(s), m_a(a) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    auto mkBtn = [&](const QString& text, const QString& tip, bool checkable) {
        auto* b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setCheckable(checkable); b->setAutoRaise(true);
        bar->addWidget(b);
        return b;
    };
    m_bMove = mkBtn(tr("Move"), tr("Move (W)"), true);
    m_bRotate = mkBtn(tr("Rotate"), tr("Rotate (E)"), true);
    m_bScale = mkBtn(tr("Scale"), tr("Scale (R)"), true);
    auto* grp = new QButtonGroup(this);
    grp->setExclusive(true);
    for (auto* b : {m_bMove, m_bRotate, m_bScale}) grp->addButton(b);
    m_bMove->setChecked(true);
    bar->addSpacing(8);
    m_bCam = mkBtn(tr("Camera View"), tr("Look through the scene camera: exactly what the composition shows"), true);
    auto* fr = mkBtn(tr("Frame"), tr("Frame the selection, or the whole scene (F)"), false);
    bar->addStretch(1);
    lay->addLayout(bar);
    m_canvas = new ViewCanvas(this);
    lay->addWidget(m_canvas, 1);
    connect(m_bMove, &QToolButton::clicked, this, [this] { setMode(Mode::Move); });
    connect(m_bRotate, &QToolButton::clicked, this, [this] { setMode(Mode::Rotate); });
    connect(m_bScale, &QToolButton::clicked, this, [this] { setMode(Mode::Scale); });
    connect(m_bCam, &QToolButton::toggled, this, [this](bool on) { setCameraView(on); });
    connect(fr, &QToolButton::clicked, this, [this] { frame(!m_s->selectedNode().isNull()); });
    connect(a->frameSelection, &QAction::triggered, this, [this] { frame(!m_s->selectedNode().isNull()); });
    auto repaint = [this] { m_canvas->update(); };
    connect(m_s, &EditorSession::activeLayerChanged, this, [this, repaint] {
        if (Layer* l = m_s->active3DLayer(); l && !m_orbits.contains(l->id)) frame(false);      // first time: show the whole scene
        repaint();
    });
    connect(m_s, &EditorSession::nodeSelectionChanged, this, repaint);
    connect(m_s, &EditorSession::documentChanged, this, [this, repaint] { if (m_s->doc()) { connect(m_s->doc(), &Document::contentChanged, this, [this] { m_canvas->update(); }); connect(m_s->doc(), &Document::frameChanged, this, [this] { m_canvas->update(); }); } repaint(); });
    if (m_s->doc()) { connect(m_s->doc(), &Document::contentChanged, this, repaint); connect(m_s->doc(), &Document::frameChanged, this, repaint); }
}

QWidget* Viewport3D::canvas() const { return m_canvas; }

OrbitCamera& Viewport3D::orbit() {
    const QUuid id = m_s->activeLayerId();
    return m_orbits[id];
}

void Viewport3D::setMode(Mode m) {
    m_mode = m;
    m_bMove->setChecked(m == Mode::Move); m_bRotate->setChecked(m == Mode::Rotate); m_bScale->setChecked(m == Mode::Scale);
    m_canvas->update();
}

void Viewport3D::setCameraView(bool on) {
    m_cameraView = on;
    if (m_bCam->isChecked() != on) m_bCam->setChecked(on);
    m_canvas->update();
}

void Viewport3D::frame(bool selectionOnly) {
    const Layer* l = m_s->active3DLayer();
    if (!l) return;
    OrbitCamera& oc = orbit();
    QVector3D mn, mx;
    bool ok = false;
    const auto shown = shownScene(*l, m_s->doc());
    const Scene3D& sc = *shown;
    if (selectionOnly) {
        const QUuid id = m_s->selectedNode();
        if (sc.node(id)) {
            QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
            std::function<void(const QUuid&)> walk = [&](const QUuid& i) {
                const Node3D* n = sc.node(i);
                if (n && n->kind == NodeKind::Mesh) if (auto it = sc.meshes.find(n->mesh); it != sc.meshes.end()) {
                    QVector3D a, b; it->second->bounds(&a, &b);
                    const QMatrix4x4 w = sc.worldMatrix(i);
                    for (int k = 0; k < 8; ++k) {
                        const QVector3D q = w.map(QVector3D(k & 1 ? b.x() : a.x(), k & 2 ? b.y() : a.y(), k & 4 ? b.z() : a.z()));
                        lo = QVector3D(std::min(lo.x(), q.x()), std::min(lo.y(), q.y()), std::min(lo.z(), q.z()));
                        hi = QVector3D(std::max(hi.x(), q.x()), std::max(hi.y(), q.y()), std::max(hi.z(), q.z()));
                        ok = true;
                    }
                }
                for (const QUuid& c : sc.childrenOf(i)) walk(c);
            };
            walk(id);
            if (!ok) { lo = hi = sc.worldMatrix(id).map(QVector3D(0, 0, 0)); lo -= QVector3D(0.5f, 0.5f, 0.5f); hi += QVector3D(0.5f, 0.5f, 0.5f); ok = true; }
            mn = lo; mx = hi;
        }
    }
    if (!ok) ok = sc.worldBounds(&mn, &mx);
    if (ok) oc.frame(mn, mx); else { oc.target = QVector3D(0, 0, 0); oc.distance = 8; }
    m_canvas->update();
}

RenderCamera Viewport3D::currentCamera(QSize* size) const {
    const Layer* l = m_s->active3DLayer();
    const QRect vr = m_canvas->viewRect();
    if (size) *size = vr.size();
    if (!l) return RenderCamera();
    return m_canvas->camera(*shownScene(*l, m_s->doc()));
}

bool Viewport3D::handlePosition(Handle h, QPointF* out) const {
    QMatrix4x4 world;
    const Layer* l = m_s->active3DLayer();
    if (!l || !m_canvas->selectedWorld(&world)) return false;
    QSize sz;
    const RenderCamera cam = currentCamera(&sz);
    const QVector3D o = world.map(QVector3D(0, 0, 0));
    const double L = m_canvas->gizmoLength(cam, o);
    QPointF p;
    if (h == Handle::Center) { if (!projectToScreen(cam, o, sz, &p)) return false; }
    else if (m_mode == Mode::Rotate) {
        const int i = int(h) - int(Handle::X);
        QVector3D u, v; ViewCanvas::planeBasis(m_canvas->axisDir(i, world), &u, &v);
        if (!projectToScreen(cam, o + (u * 0.7071f + v * 0.7071f) * float(L * 0.9), sz, &p)) return false;
    } else {
        const int i = int(h) - int(Handle::X);
        if (!projectToScreen(cam, o + m_canvas->axisDir(i, world) * float(L), sz, &p)) return false;
    }
    *out = p + m_canvas->viewRect().topLeft();
    return true;
}

}  // namespace mynd
