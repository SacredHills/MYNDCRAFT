#include <QGuiApplication>
#include "UI/Tools.h"

#include <QLinearGradient>
#include <cmath>

#include "Drawing/FloodFill.h"
#include "Graphics/Compositor.h"
#include "UI/CanvasView.h"

namespace mynd {

namespace {

constexpr double kHitPx = 6.0;

QRectF normalized(const QPointF& a, const QPointF& b) { return QRectF(a, b).normalized(); }

// ============================================================ Brush / Pencil / Eraser
class BrushTool : public Tool {
public:
    BrushTool(ToolId id, EditorSession* s, CanvasView* c) : Tool(s, c), m_id(id) {}
    ToolId id() const override { return m_id; }
    bool busy() const override { return bool(m_stroke); }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton || m_stroke) return;
        Layer* l = m_s->editableLayer(LayerType::Raster, true);
        if (!l) return;
        m_layer = l->id;
        QImage* target = m_s->doc()->editImage(l);
        if (!target) return;
        m_drawing = l->type == LayerType::Animation ? l->anim.drawingAt(m_s->doc()->currentFrame()) : QUuid();
        BrushSettings b = m_s->brush(m_id);
        const bool erase = m_id == ToolId::Eraser || e.eraserEnd;
        const BrushMode mode = m_id == ToolId::Smudge ? BrushMode::Smudge : erase ? BrushMode::Erase : BrushMode::Paint;
        m_stroke = std::make_unique<BrushStroke>(target, b, m_s->foreground(), mode);
        m_erase = erase;
        m_stroke->begin(e.doc, e.pressure);
        m_s->doc()->emitContentChanged(m_stroke->lastUpdateRect());
    }
    void move(const PointerEvent& e) override {
        if (!m_stroke) { m_c->update(); return; }
        m_stroke->addPoint(e.doc, e.pressure);
        m_s->doc()->emitContentChanged(m_stroke->lastUpdateRect());
    }
    void release(const PointerEvent& e) override {
        if (!m_stroke) return;
        m_stroke->addPoint(e.doc, e.pressure);
        m_stroke->end();
        m_s->doc()->emitContentChanged(m_stroke->lastUpdateRect());
        const QRect dirty = m_stroke->dirtyRect();
        const QImage before = m_stroke->original();
        m_stroke.reset();
        m_s->doc()->commitRasterPatch(m_layer, dirty, before, m_id == ToolId::Smudge ? QObject::tr("Smudge") : m_erase ? QObject::tr("Erase") : QObject::tr("Brush Stroke"));
    }
    void cancel() override {
        if (!m_stroke) return;
        // Restore the layer to its pre-stroke state.
        if (Layer* l = m_s->doc()->findLayer(m_layer)) {
            QImage* img = l->type == LayerType::Animation ? l->drawingImage(m_drawing) : &l->raster;
            if (img) *img = m_stroke->original().copy();
        }
        m_stroke.reset();
        m_s->doc()->emitContentChanged({});
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        if (!m_c->hasHover()) return;
        const BrushSettings b = m_s->brush(m_id);
        const double r = std::max(1.0, b.size * m_c->zoom() / 2.0);
        const QPointF c = t.map(m_c->hoverDocPos());
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 200), 1));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawEllipse(c, r + 1, r + 1);
    }
    QCursor cursor() const override { return Qt::BlankCursor; }

private:
    ToolId m_id;
    std::unique_ptr<BrushStroke> m_stroke;
    QUuid m_layer, m_drawing;
    bool m_erase = false;
};

// ============================================================ Eyedropper
class EyedropperTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Eyedropper; }
    void press(const PointerEvent& e) override { m_down = true; sample(e); }
    void move(const PointerEvent& e) override { if (m_down) sample(e); }
    void release(const PointerEvent&) override { m_down = false; }
private:
    void sample(const PointerEvent& e) {
        const QPoint p = e.doc.toPoint();
        if (!m_s->doc()->canvasRect().contains(p)) return;
        const QColor c = m_c->sampleColor(p);
        if (e.alt()) m_s->setBackground(c); else m_s->setForeground(c);
    }
    bool m_down = false;
};

// ============================================================ Hand / Zoom
class HandTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Hand; }
    void press(const PointerEvent& e) override { m_last = e.view; m_down = true; }
    void move(const PointerEvent& e) override {
        if (!m_down) return;
        m_c->panBy(e.view - m_last);
        m_last = e.view;
    }
    void release(const PointerEvent&) override { m_down = false; }
    QCursor cursor() const override { return m_down ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }
private:
    QPointF m_last; bool m_down = false;
};

class ZoomTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Zoom; }
    void press(const PointerEvent& e) override {
        m_c->zoomAt(e.alt() || e.button == Qt::RightButton ? 1 / 1.5 : 1.5, e.view);
    }
    QCursor cursor() const override { return Qt::PointingHandCursor; }
};

// ============================================================ Paint bucket
class BucketTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Bucket; }
    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = m_s->activeLayer();
        if (!l || l->isGroup() || l->type == LayerType::Text) { emit_msg(QObject::tr("Select a raster or vector layer.")); return; }
        if (l->props.locked || !l->props.visible) { emit_msg(QObject::tr("The active layer is locked or hidden.")); return; }
        if (l->isPixelLayer()) {
            QImage* img = m_s->doc()->editImage(l);
            if (!img) { emit_msg(QObject::tr("There is no drawing at this frame yet. Draw something first.")); return; }
            const QPoint p = e.doc.toPoint();
            const QImage before = img->copy();
            const QRect r = floodFill(*img, p, m_s->foreground(), m_s->bucketTolerance);
            if (r.isEmpty()) return;
            m_s->doc()->commitRasterPatch(l->id, r, before, QObject::tr("Paint Bucket"));
            m_s->doc()->emitContentChanged(r);
        } else {
            ShapeList after = l->shapes;
            for (int i = after.size() - 1; i >= 0; --i) {
                if (after[i].hitTest(e.doc, kHitPx / m_c->zoom())) {
                    after[i].fill.type = Paint::Solid;
                    after[i].fill.color = m_s->foreground();
                    m_s->doc()->setShapes(l->id, after, QObject::tr("Fill Shape"));
                    return;
                }
            }
        }
    }
private:
    void emit_msg(const QString& s) { emit m_s->message(s); }
};

// ============================================================ Gradient
class GradientTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Gradient; }
    bool busy() const override { return m_down; }
    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = m_s->activeLayer();
        if (!l || l->isGroup() || l->type == LayerType::Text || l->props.locked || !l->props.visible) {
            emit m_s->message(QObject::tr("Select an unlocked, visible raster or vector layer.")); return;
        }
        m_a = m_b = e.doc; m_down = true;
    }
    void move(const PointerEvent& e) override { if (m_down) { m_b = e.doc; m_c->update(); } }
    void release(const PointerEvent& e) override {
        if (!m_down) return;
        m_down = false;
        m_b = e.doc;
        Layer* l = m_s->activeLayer();
        if (!l || QLineF(m_a, m_b).length() < 2) { m_c->update(); return; }
        if (l->isPixelLayer()) {
            QImage* img = m_s->doc()->editImage(l);
            if (!img) { emit m_s->message(QObject::tr("There is no drawing at this frame yet. Draw something first.")); m_c->update(); return; }
            const QImage before = img->copy();
            QLinearGradient g(m_a, m_b);
            g.setColorAt(0, m_s->foreground());
            g.setColorAt(1, m_s->background());
            QPainter p(img);
            p.fillRect(img->rect(), g);
            p.end();
            m_s->doc()->commitRasterPatch(l->id, img->rect(), before, QObject::tr("Gradient Fill"));
            m_s->doc()->emitContentChanged({});
        } else {
            ShapeList after = l->shapes;
            // Prefer the shape under the drag start; otherwise use the current selection.
            QSet<QUuid> target;
            for (int i = after.size() - 1; i >= 0; --i)
                if (after[i].hitTest(m_a, kHitPx / m_c->zoom())) { target = m_s->expandGroups(*l, {after[i].id}); break; }
            if (target.isEmpty()) target = m_s->selectedShapes();
            if (target.isEmpty()) { emit m_s->message(QObject::tr("Click-drag on a shape, or select shapes first.")); return; }
            for (auto& s : after) {
                if (!target.contains(s.id) || !s.closed) continue;
                s.fill.type = Paint::LinearGradient;
                s.fill.gradStart = m_a;
                s.fill.gradEnd = m_b;
                s.fill.stops = {{0, m_s->foreground()}, {1, m_s->background()}};
            }
            m_s->doc()->setShapes(l->id, after, QObject::tr("Gradient Fill"));
        }
        m_c->update();
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        if (!m_down) return;
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(t.map(m_a), t.map(m_b));
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(m_s->foreground()); p.drawEllipse(t.map(m_a), 5, 5);
        p.setBrush(m_s->background()); p.drawEllipse(t.map(m_b), 5, 5);
    }
private:
    QPointF m_a, m_b; bool m_down = false;
};

// ============================================================ Vector selection helpers
Layer* activeVector(EditorSession* s) {
    Layer* l = s->activeLayer();
    return (l && l->type == LayerType::Vector) ? l : nullptr;
}

int topmostHit(const Layer& l, const QPointF& p, double tol) {
    for (int i = l.shapes.size() - 1; i >= 0; --i)
        if (l.shapes[i].hitTest(p, tol)) return i;
    return -1;
}

void drawSquare(QPainter& p, const QPointF& c, double half, bool filled) {
    p.setPen(QPen(QColor(20, 120, 255), 1.5));
    p.setBrush(filled ? QColor(20, 120, 255) : QColor(255, 255, 255));
    p.drawRect(QRectF(c.x() - half, c.y() - half, 2 * half, 2 * half));
}

// ============================================================ Selection tool
class SelectTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Select; }
    bool busy() const override { return m_mode != Idle; }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        if (Layer* tl = m_s->activeLayer(); tl && tl->type == LayerType::Text) {   // text layers move as a unit
            if (tl->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return; }
            if (tl->textContent.bounds().adjusted(-4, -4, 4, 4).contains(e.doc)) {
                m_mode = MovingText; m_start = e.doc; m_textBefore = tl->textContent;
            }
            return;
        }
        Layer* l = activeVector(m_s);
        if (!l) { emit m_s->message(QObject::tr("Select a vector or text layer to move things.")); return; }
        const int hit = topmostHit(*l, e.doc, kHitPx / m_c->zoom());
        QSet<QUuid> sel = m_s->selectedShapes();
        if (hit >= 0) {
            const QSet<QUuid> ids = m_s->expandGroups(*l, {l->shapes[hit].id});
            if (e.shift()) {
                if (sel.contains(l->shapes[hit].id)) sel.subtract(ids); else sel.unite(ids);
            } else if (!sel.contains(l->shapes[hit].id)) {
                sel = ids;
            }
            m_s->setSelectedShapes(sel);
            if (l->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return; }
            if (!m_s->selectedShapes().isEmpty()) {
                m_mode = Moving;
                m_start = e.doc;
                m_before = l->shapes;
                m_anchor = l->shapes[hit].bounds().topLeft();
            }
        } else {
            if (!e.shift()) m_s->clearSelection();
            m_mode = Marquee;
            m_start = m_cur = e.doc;
            m_base = m_s->selectedShapes();
        }
    }
    void move(const PointerEvent& e) override {
        if (m_mode == MovingText) {
            Layer* tl = m_s->activeLayer();
            if (!tl || tl->type != LayerType::Text) return;
            QPointF d = e.doc - m_start;
            if (e.shift()) { if (std::abs(d.x()) > std::abs(d.y())) d.setY(0); else d.setX(0); }
            tl->textContent.pos = m_c->snap(m_textBefore.pos + d);
            m_s->doc()->emitContentChanged({});
            return;
        }
        Layer* l = activeVector(m_s);
        if (!l) return;
        if (m_mode == Moving) {
            QPointF d = e.doc - m_start;
            if (e.shift()) { if (std::abs(d.x()) > std::abs(d.y())) d.setY(0); else d.setX(0); }
            else if (m_s->snapEnabled) d = m_c->snap(m_anchor + d) - m_anchor;
            ShapeList s = m_before;
            for (auto& sh : s) if (m_s->selectedShapes().contains(sh.id)) sh.translate(d);
            l->shapes = s;
            m_s->doc()->emitContentChanged({});
        } else if (m_mode == Marquee) {
            m_cur = e.doc;
            QSet<QUuid> sel = m_base;
            const QRectF r = normalized(m_start, m_cur);
            for (const auto& sh : l->shapes) if (sh.bounds().intersects(r)) sel.insert(sh.id);
            m_s->setSelectedShapes(m_s->expandGroups(*l, sel), false);
            m_c->update();
        }
    }
    void release(const PointerEvent&) override {
        if (m_mode == MovingText) {
            Layer* tl = m_s->activeLayer();
            if (tl && tl->type == LayerType::Text) {
                const TextContent after = tl->textContent;
                tl->textContent = m_textBefore;                       // let the command apply it so undo is exact
                m_s->doc()->setTextContent(tl->id, after, QObject::tr("Move Text"));
                m_s->doc()->emitContentChanged({});
            }
            m_mode = Idle;
            m_c->update();
            return;
        }
        Layer* l = activeVector(m_s);
        if (m_mode == Moving && l) {
            ShapeList after = l->shapes;
            if (after != m_before) m_s->doc()->setShapes(l->id, after, QObject::tr("Move Shapes"), &m_before);
        }
        m_mode = Idle;
        m_c->update();
    }
    void cancel() override {
        if (m_mode == Moving) if (Layer* l = activeVector(m_s)) { l->shapes = m_before; m_s->doc()->emitContentChanged({}); }
        if (m_mode == MovingText) if (Layer* l = m_s->activeLayer()) if (l->type == LayerType::Text) { l->textContent = m_textBefore; m_s->doc()->emitContentChanged({}); }
        m_mode = Idle;
    }
    bool keyPress(QKeyEvent* k) override {
        Layer* l = activeVector(m_s);
        if (!l || l->props.locked || m_s->selectedShapes().isEmpty()) return false;
        ShapeList after = l->shapes;
        if (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace) {
            const auto sel = m_s->selectedShapes();
            after.erase(std::remove_if(after.begin(), after.end(), [&](const VectorShape& s) { return sel.contains(s.id); }), after.end());
            m_s->doc()->setShapes(l->id, after, QObject::tr("Delete Shapes"));
            m_s->clearSelection();
            return true;
        }
        QPointF d;
        const double step = (k->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        switch (k->key()) {
        case Qt::Key_Left: d = {-step, 0}; break;
        case Qt::Key_Right: d = {step, 0}; break;
        case Qt::Key_Up: d = {0, -step}; break;
        case Qt::Key_Down: d = {0, step}; break;
        default: return false;
        }
        for (auto& s : after) if (m_s->selectedShapes().contains(s.id)) s.translate(d);
        m_s->doc()->setShapes(l->id, after, QObject::tr("Nudge Shapes"));
        return true;
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        if (m_mode == Marquee) {
            p.setPen(QPen(QColor(20, 120, 255), 1, Qt::DashLine));
            p.setBrush(QColor(20, 120, 255, 30));
            p.drawRect(t.mapRect(normalized(m_start, m_cur)));
        }
    }
private:
    enum Mode { Idle, Moving, Marquee, MovingText } m_mode = Idle;
    QPointF m_start, m_cur, m_anchor;
    ShapeList m_before;
    TextContent m_textBefore;
    QSet<QUuid> m_base;
};

// ============================================================ Direct selection
class DirectSelectTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::DirectSelect; }
    bool busy() const override { return m_drag != None; }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = activeVector(m_s);
        if (!l) { emit m_s->message(QObject::tr("Select a vector layer to edit paths.")); return; }
        const double tol = kHitPx / m_c->zoom();
        VectorShape* cur = current(*l);
        if (cur) {
            // Handles of the selected node take priority, then nodes.
            if (m_node >= 0 && m_node < cur->nodes.size()) {
                const PathNode& n = cur->nodes[m_node];
                if (!n.out.isNull() && QLineF(e.doc, n.pos + n.out).length() <= tol) return begin(l, HandleOut);
                if (!n.in.isNull() && QLineF(e.doc, n.pos + n.in).length() <= tol) return begin(l, HandleIn);
            }
            for (int i = 0; i < cur->nodes.size(); ++i) {
                if (QLineF(e.doc, cur->nodes[i].pos).length() <= tol) { m_node = i; emit m_s->selectionChanged(); return begin(l, Node); }
            }
            // Press on the outline of the selected shape and drag: bend that segment (no point needed).
            const VectorShape::SegmentHit sh = cur->nearestSegment(e.doc);
            if (sh.seg >= 0 && sh.distance <= tol) {
                m_seg = sh.seg; m_segT = std::clamp(sh.t, 0.15, 0.85); m_segPress = sh.point;
                const int j = (sh.seg + 1) % cur->nodes.size();
                m_segOut = cur->nodes[sh.seg].out; m_segIn = cur->nodes[j].in;
                m_node = -1;
                return begin(l, Segment);
            }
        }
        const int hit = topmostHit(*l, e.doc, tol);
        if (hit >= 0) { m_s->setSelectedShapes({l->shapes[hit].id}); m_node = -1; }
        else { m_s->clearSelection(); m_node = -1; }
        m_c->update();
    }
    void move(const PointerEvent& e) override {
        if (m_drag == None) return;
        Layer* l = activeVector(m_s);
        VectorShape* s = l ? current(*l) : nullptr;
        if (m_drag == Segment) {
            if (!s || m_seg < 0 || m_seg >= s->nodes.size()) return;
            const int n = s->nodes.size(), j = (m_seg + 1) % n;
            // Moving both control points by d / (3 t (1-t)) moves the curve point at t by exactly d.
            const QPointF d = (e.doc - m_segPress) / (3.0 * m_segT * (1.0 - m_segT));
            PathNode& a = s->nodes[m_seg]; PathNode& b = s->nodes[j];
            a.out = m_segOut + d;
            b.in = m_segIn + d;
            if (a.smooth) a.in = -a.out;
            if (b.smooth) b.out = -b.in;
            m_s->doc()->emitContentChanged({});
            return;
        }
        if (!s || m_node < 0 || m_node >= s->nodes.size()) return;
        PathNode& n = s->nodes[m_node];
        if (m_drag == Node) {
            n.pos = m_c->snap(e.doc);
        } else {
            const QPointF off = e.doc - n.pos;
            if (m_drag == HandleOut) { n.out = off; if (n.smooth && !e.alt()) n.in = -off; }
            else { n.in = off; if (n.smooth && !e.alt()) n.out = -off; }
        }
        m_s->doc()->emitContentChanged({});
    }
    void release(const PointerEvent&) override {
        if (m_drag == None) return;
        Layer* l = activeVector(m_s);
        if (l && l->shapes != m_before) m_s->doc()->setShapes(l->id, l->shapes, QObject::tr("Edit Path"), &m_before);
        m_drag = None;
        m_c->update();
    }
    void doubleClick(const PointerEvent& e) override {
        Layer* l = activeVector(m_s);
        VectorShape* s = l ? current(*l) : nullptr;
        if (!s || l->props.locked) return;
        for (int i = 0; i < s->nodes.size(); ++i) {
            if (QLineF(e.doc, s->nodes[i].pos).length() > kHitPx / m_c->zoom()) continue;
            const ShapeList before = l->shapes;
            l->shapes.detach();                       // `s` pointed into the shared buffer; re-point after detaching
            s = current(*l);
            PathNode& n = s->nodes[i];
            if (n.smooth || !n.in.isNull() || !n.out.isNull()) { n.in = n.out = {}; n.smooth = false; }
            else {
                const int cnt = s->nodes.size();
                const bool hasPrev = s->closed || i > 0, hasNext = s->closed || i < cnt - 1;
                const QPointF prev = hasPrev ? s->nodes[(i + cnt - 1) % cnt].pos : n.pos;
                const QPointF next = hasNext ? s->nodes[(i + 1) % cnt].pos : n.pos;
                QPointF dir = next - prev;
                const double len = std::hypot(dir.x(), dir.y());
                if (len > 0) {
                    const double h = std::min(QLineF(prev, n.pos).length(), QLineF(n.pos, next).length()) / 3.0;
                    dir = dir / len * h;
                    n.out = dir; n.in = -dir; n.smooth = true;
                }
            }
            m_node = i;
            m_s->doc()->setShapes(l->id, l->shapes, QObject::tr("Toggle Smooth Point"), &before);
            return;
        }
        // Double-click on the outline (not on a point) adds a point there without changing the shape.
        const VectorShape::SegmentHit hit = s->nearestSegment(e.doc);
        if (hit.seg >= 0 && hit.distance <= kHitPx / m_c->zoom()) {
            const ShapeList before = l->shapes;
            l->shapes.detach();
            s = current(*l);
            const int idx = s->insertNode(hit.seg, hit.t);
            if (idx >= 0) {
                m_node = idx;
                m_s->doc()->setShapes(l->id, l->shapes, QObject::tr("Add Point"), &before);
                emit m_s->selectionChanged();
            }
        }
    }
    bool keyPress(QKeyEvent* k) override {
        if (k->key() != Qt::Key_Delete && k->key() != Qt::Key_Backspace) return false;
        Layer* l = activeVector(m_s);
        VectorShape* s = l ? current(*l) : nullptr;
        if (!s || l->props.locked) return false;
        ShapeList after = l->shapes;
        const QUuid id = s->id;
        for (int i = 0; i < after.size(); ++i) {
            if (after[i].id != id) continue;
            if (m_node >= 0 && after[i].nodes.size() > 2) { after[i].nodes.removeAt(m_node); m_node = -1; }
            else { after.removeAt(i); m_s->clearSelection(); m_node = -1; }
            break;
        }
        m_s->doc()->setShapes(l->id, after, QObject::tr("Delete Point"));
        return true;
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        Layer* l = activeVector(m_s);
        VectorShape* s = l ? current(*l) : nullptr;
        if (!s) return;
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(20, 120, 255), 1));
        p.drawPath(t.map(s->path()));
        for (int i = 0; i < s->nodes.size(); ++i) {
            const PathNode& n = s->nodes[i];
            const QPointF c = t.map(n.pos);
            if (i == m_node) {
                p.setPen(QPen(QColor(20, 120, 255), 1));
                for (const QPointF& h : {n.in, n.out}) {
                    if (h.isNull()) continue;
                    const QPointF hp = t.map(n.pos + h);
                    p.drawLine(c, hp);
                    p.setBrush(Qt::white); p.drawEllipse(hp, 4, 4);
                }
            }
            drawSquare(p, c, 4, i == m_node);
        }
    }
private:
    enum Drag { None, Node, HandleIn, HandleOut, Segment };
    int m_seg = -1; double m_segT = 0.5; QPointF m_segPress, m_segOut, m_segIn;
    VectorShape* current(Layer& l) {
        for (auto& s : l.shapes) if (m_s->selectedShapes().contains(s.id)) return &s;
        return nullptr;
    }
    void begin(Layer* l, Drag d) {
        if (l->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return; }
        m_drag = d; m_before = l->shapes; m_c->update();
    }
    Drag m_drag = None;
    int m_node = -1;
    ShapeList m_before;
};

// ============================================================ Pen tool
class PenTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Pen; }
    bool busy() const override { return !m_nodes.isEmpty(); }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        const QPointF p = m_c->snap(e.doc);
        if (m_nodes.isEmpty()) {
            Layer* l = m_s->editableLayer(LayerType::Vector, true);
            if (!l) return;
            m_layer = l->id;
        } else if (m_nodes.size() >= 2 && QLineF(p, m_nodes[0].pos).length() <= kHitPx / m_c->zoom()) {
            // Clicking the first node closes the path; dragging there would shape the closing handle (not supported).
            return finish(true);
        }
        PathNode n; n.pos = p;
        m_nodes.append(n);
        m_dragging = true;
        m_c->update();
    }
    void move(const PointerEvent& e) override {
        m_hover = m_c->snap(e.doc);
        if (m_dragging && !m_nodes.isEmpty()) {
            PathNode& n = m_nodes.last();
            const QPointF off = m_hover - n.pos;
            if (std::hypot(off.x(), off.y()) * m_c->zoom() > 3) { n.out = off; n.in = -off; n.smooth = true; }
        }
        m_c->update();
    }
    void release(const PointerEvent&) override { m_dragging = false; }
    void doubleClick(const PointerEvent&) override {
        // The second click of a double-click already added a duplicate node at the same spot.
        if (m_nodes.size() >= 2 && QLineF(m_nodes.last().pos, m_nodes[m_nodes.size() - 2].pos).length() < 1.0) m_nodes.removeLast();
        finish(false);
    }
    bool keyPress(QKeyEvent* k) override {
        if (m_nodes.isEmpty()) return false;
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { finish(false); return true; }
        if (k->key() == Qt::Key_Escape) { cancel(); return true; }
        if (k->key() == Qt::Key_Backspace) { m_nodes.removeLast(); m_c->update(); return true; }
        return false;
    }
    void cancel() override { m_nodes.clear(); m_dragging = false; m_c->update(); }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        if (m_nodes.isEmpty()) return;
        p.setRenderHint(QPainter::Antialiasing);
        VectorShape s; s.nodes = m_nodes; s.closed = false;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(20, 120, 255), 1.5));
        p.drawPath(t.map(s.path()));
        if (!m_dragging) {  // rubber band to the cursor
            const PathNode& last = m_nodes.last();
            QPainterPath rb; rb.moveTo(last.pos);
            if (last.out.isNull()) rb.lineTo(m_hover); else rb.cubicTo(last.pos + last.out, m_hover, m_hover);
            p.setPen(QPen(QColor(20, 120, 255, 150), 1, Qt::DashLine));
            p.drawPath(t.map(rb));
        }
        for (int i = 0; i < m_nodes.size(); ++i) {
            drawSquare(p, t.map(m_nodes[i].pos), 4, i == m_nodes.size() - 1);
            if (i == m_nodes.size() - 1 && !m_nodes[i].out.isNull()) {
                p.setPen(QPen(QColor(20, 120, 255), 1));
                for (const QPointF& h : {m_nodes[i].in, m_nodes[i].out}) {
                    p.drawLine(t.map(m_nodes[i].pos), t.map(m_nodes[i].pos + h));
                    p.setBrush(Qt::white); p.drawEllipse(t.map(m_nodes[i].pos + h), 3.5, 3.5);
                }
            }
        }
    }
private:
    void finish(bool closed) {
        m_dragging = false;
        Layer* l = m_s->doc()->findLayer(m_layer);
        if (l && l->type == LayerType::Vector && m_nodes.size() >= 2) {
            VectorShape s; s.name = QObject::tr("Path"); s.nodes = m_nodes; s.closed = closed;
            s = m_s->styled(s);
            ShapeList after = l->shapes; after.append(s);
            m_s->doc()->setShapes(l->id, after, QObject::tr("Draw Path"));
            m_s->setSelectedShapes({s.id}, false);
        }
        m_nodes.clear();
        m_c->update();
    }
    QVector<PathNode> m_nodes;
    QUuid m_layer;
    bool m_dragging = false;
    QPointF m_hover;
};

// ============================================================ Shape tools (line/rect/ellipse/polygon)
class ShapeTool : public Tool {
public:
    ShapeTool(ToolId id, EditorSession* s, CanvasView* c) : Tool(s, c), m_id(id) {}
    ToolId id() const override { return m_id; }
    bool busy() const override { return m_down; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = m_s->editableLayer(LayerType::Vector, true);
        if (!l) return;
        m_layer = l->id;
        m_a = m_b = m_c->snap(e.doc);
        m_down = true;
    }
    void move(const PointerEvent& e) override {
        if (!m_down) return;
        m_b = m_c->snap(e.doc);
        m_shift = e.shift(); m_alt = e.alt();
        m_c->update();
    }
    void release(const PointerEvent& e) override {
        if (!m_down) return;
        m_down = false;
        m_b = m_c->snap(e.doc);
        m_shift = e.shift(); m_alt = e.alt();
        Layer* l = m_s->doc()->findLayer(m_layer);
        VectorShape s = build();
        if (l && l->type == LayerType::Vector && QLineF(m_a, m_b).length() * m_c->zoom() >= 2 && !s.nodes.isEmpty()) {
            s = m_s->styled(s);
            if (m_id == ToolId::Line) s.fill.type = Paint::None;
            ShapeList after = l->shapes; after.append(s);
            m_s->doc()->setShapes(l->id, after, QObject::tr("Draw %1").arg(s.name));
            m_s->setSelectedShapes({s.id}, false);
        }
        m_c->update();
    }
    void cancel() override { m_down = false; m_c->update(); }
    bool keyPress(QKeyEvent* k) override {
        if (m_down && k->key() == Qt::Key_Escape) { cancel(); return true; }
        return false;
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        if (!m_down) return;
        VectorShape s = m_s->styled(build());
        if (m_id == ToolId::Line) s.fill.type = Paint::None;
        p.setRenderHint(QPainter::Antialiasing);
        p.save();
        p.setTransform(t);
        Compositor::drawShape(p, s);
        p.restore();
    }
private:
    VectorShape build() const {
        QPointF a = m_a, b = m_b;
        if (m_id == ToolId::Line) {
            if (m_shift) {  // snap angle to 45 degree steps
                const QPointF d = b - a;
                const double ang = std::round(std::atan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4);
                const double len = std::hypot(d.x(), d.y());
                b = a + QPointF(std::cos(ang), std::sin(ang)) * len;
            }
            return VectorShape::makeLine(a, b);
        }
        if (m_id == ToolId::Polygon) {
            const double r = QLineF(a, b).length();
            const double rot = m_shift ? -M_PI / 2 : std::atan2(b.y() - a.y(), b.x() - a.x());
            return VectorShape::makePolygon(a, r, m_s->polygonSides, rot);
        }
        QRectF r;
        if (m_shift) {  // square / circle
            const double side = std::max(std::abs(b.x() - a.x()), std::abs(b.y() - a.y()));
            b = a + QPointF(b.x() >= a.x() ? side : -side, b.y() >= a.y() ? side : -side);
        }
        if (m_alt) { const QPointF d = b - a; r = QRectF(a - d, a + d).normalized(); }
        else r = normalized(a, b);
        return m_id == ToolId::Rectangle ? VectorShape::makeRect(r) : VectorShape::makeEllipse(r);
    }
    ToolId m_id;
    QPointF m_a, m_b;
    bool m_down = false, m_shift = false, m_alt = false;
    QUuid m_layer;
};

// ============================================================ Free transform
QRect contentBounds(const QImage& img) {
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y) {
        const auto* row = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (row[x] >> 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        }
    }
    return x1 < 0 ? QRect() : QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

class FreeTransformTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::FreeTransform; }
    bool busy() const override { return m_h != None; }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = m_s->activeLayer();
        const QRectF box = currentBox();
        if (!l || box.isNull()) { emit m_s->message(QObject::tr("Select shapes on a vector layer, or a raster layer with content, to transform.")); return; }
        if (l->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return; }
        m_h = hitHandle(box, e.doc);
        if (m_h == None) return;
        m_box0 = box; m_start = e.doc;
        if (l->type == LayerType::Vector) m_before = l->shapes;
        else if (QImage* img = m_s->doc()->editImage(l)) m_orig = img->copy();
        else { m_h = None; return; }
    }
    void move(const PointerEvent& e) override {
        if (m_h == None) return;
        Layer* l = m_s->activeLayer();
        if (!l) return;
        m_t = compute(e);
        apply(l);
        m_c->update();
    }
    void release(const PointerEvent& e) override {
        if (m_h == None) return;
        Layer* l = m_s->activeLayer();
        if (l) {
            m_t = compute(e);
            apply(l);
            if (l->type == LayerType::Vector) {
                const ShapeList after = l->shapes;
                if (after != m_before) m_s->doc()->setShapes(l->id, after, QObject::tr("Transform Shapes"), &m_before);
            } else if (!m_t.isIdentity() && !m_orig.isNull()) {
                m_s->doc()->commitRasterPatch(l->id, m_orig.rect(), m_orig, QObject::tr("Transform Layer"));
            }
        }
        m_h = None; m_orig = QImage(); m_cacheIndex = -2;
        m_c->update();
    }
    void cancel() override {
        if (m_h == None) return;
        Layer* l = m_s->activeLayer();
        if (l) { m_t = QTransform(); apply(l); }
        m_h = None; m_orig = QImage();
    }
    bool keyPress(QKeyEvent* k) override {
        if (k->key() == Qt::Key_Escape && m_h != None) { cancel(); return true; }
        return false;
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        QRectF box = m_h != None ? m_box0 : currentBox();
        if (box.isNull()) return;
        QPolygonF poly = QPolygonF(box);
        if (m_h != None) poly = m_t.map(poly);
        const QPolygonF v = t.map(poly);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(20, 120, 255), 1));
        p.drawPolygon(v);
        // eight handles at corners and edge midpoints
        QVector<QPointF> pts;
        for (int i = 0; i < 4; ++i) { pts << v[i]; pts << (v[i] + v[(i + 1) % 4]) / 2; }
        for (const QPointF& q : pts) drawSquare(p, q, 4, false);
    }

private:
    enum Handle { None, Move, Rotate, TL, T, TR, R, BR, B, BL, L };
    static QPointF handlePoint(const QRectF& b, Handle h) {
        switch (h) {
        case TL: return b.topLeft(); case T: return {b.center().x(), b.top()}; case TR: return b.topRight();
        case R: return {b.right(), b.center().y()}; case BR: return b.bottomRight();
        case B: return {b.center().x(), b.bottom()}; case BL: return b.bottomLeft();
        case L: return {b.left(), b.center().y()}; default: return b.center();
        }
    }
    static Handle opposite(Handle h) {
        switch (h) { case TL: return BR; case T: return B; case TR: return BL; case R: return L;
                     case BR: return TL; case B: return T; case BL: return TR; case L: return R; default: return h; }
    }
    Handle hitHandle(const QRectF& b, const QPointF& p) const {
        const double tol = 7.0 / m_c->zoom();
        for (Handle h : {TL, TR, BR, BL, T, R, B, L})
            if (QLineF(p, handlePoint(b, h)).length() <= tol) return h;
        if (b.adjusted(-tol / 2, -tol / 2, tol / 2, tol / 2).contains(p)) return Move;
        const double rz = 26.0 / m_c->zoom();
        if (b.adjusted(-rz, -rz, rz, rz).contains(p)) return Rotate;   // just outside the box
        return None;
    }
    QTransform compute(const PointerEvent& e) const {
        QTransform t;
        if (m_h == Move) {
            QPointF d = e.doc - m_start;
            if (e.shift()) { if (std::abs(d.x()) > std::abs(d.y())) d.setY(0); else d.setX(0); }
            t.translate(d.x(), d.y());
        } else if (m_h == Rotate) {
            const QPointF c = m_box0.center();
            double deg = qRadiansToDegrees(std::atan2(e.doc.y() - c.y(), e.doc.x() - c.x()) - std::atan2(m_start.y() - c.y(), m_start.x() - c.x()));
            if (e.shift()) deg = std::round(deg / 15.0) * 15.0;
            t.translate(c.x(), c.y()); t.rotate(deg); t.translate(-c.x(), -c.y());
        } else {
            const QPointF h0 = handlePoint(m_box0, m_h);
            const QPointF a = e.alt() ? m_box0.center() : handlePoint(m_box0, opposite(m_h));
            double sx = 1, sy = 1;
            const bool horiz = m_h != T && m_h != B, vert = m_h != L && m_h != R;
            if (horiz && std::abs(h0.x() - a.x()) > 1e-6) sx = (e.doc.x() - a.x()) / (h0.x() - a.x());
            if (vert && std::abs(h0.y() - a.y()) > 1e-6) sy = (e.doc.y() - a.y()) / (h0.y() - a.y());
            if (e.shift()) {   // keep proportions
                const double s = (horiz && vert) ? (std::abs(sx) > std::abs(sy) ? sx : sy) : (horiz ? sx : sy);
                sx = sy = s;
            }
            if (std::abs(sx) < 0.01) sx = 0.01 * (sx < 0 ? -1 : 1);
            if (std::abs(sy) < 0.01) sy = 0.01 * (sy < 0 ? -1 : 1);
            t.translate(a.x(), a.y()); t.scale(sx, sy); t.translate(-a.x(), -a.y());
        }
        return t;
    }
    void apply(Layer* l) {
        if (l->type == LayerType::Vector) {
            ShapeList s = m_before;
            for (auto& sh : s) if (m_s->selectedShapes().contains(sh.id)) sh.transform(m_t);
            l->shapes = s;
        } else {
            QImage* img = m_s->doc()->editImage(l);
            if (!img || m_orig.isNull()) return;
            img->fill(Qt::transparent);
            QPainter p(img);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setTransform(m_t);
            p.drawImage(0, 0, m_orig);
        }
        m_s->doc()->emitContentChanged({});
    }
    QRectF currentBox() {
        Layer* l = m_s->activeLayer();
        if (!l) return {};
        if (l->type == LayerType::Vector) {
            QRectF r;
            for (const auto& s : l->shapes) if (m_s->selectedShapes().contains(s.id)) r = r.united(s.path().boundingRect());
            return r;
        }
        if (l->isPixelLayer()) {
            const QImage* img = m_s->doc()->editImage(l);
            if (!img || img->isNull()) return {};
            const int idx = m_s->doc()->undoStack()->index();
            const int frame = m_s->doc()->currentFrame();
            if (m_cacheLayer != l->id || m_cacheIndex != idx || m_cacheFrame != frame) {
                m_cacheBox = contentBounds(*img); m_cacheLayer = l->id; m_cacheIndex = idx; m_cacheFrame = frame;
            }
            return QRectF(m_cacheBox);
        }
        return {};
    }
    Handle m_h = None;
    QRectF m_box0;
    QPointF m_start;
    QTransform m_t;
    ShapeList m_before;
    QImage m_orig;
    QRect m_cacheBox; QUuid m_cacheLayer; int m_cacheIndex = -2, m_cacheFrame = -1;
};

// ============================================================ Text
class TextTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Text; }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* hit = nullptr;
        Document::forEachLayer(m_s->doc()->root(), [&](Layer* l) {
            if (l->type == LayerType::Text && l->props.visible && l->textContent.bounds().adjusted(-4, -4, 4, 4).contains(e.doc)) hit = l;
        });
        if (hit) {
            if (hit->props.locked) { emit m_s->message(QObject::tr("That text layer is locked.")); return; }
            m_s->setActiveLayer(hit->id);
            m_c->beginTextEdit(hit->id, hit->textContent.pos);
        } else {
            m_c->beginTextEdit({}, m_c->snap(e.doc));
        }
    }
};

// ============================================================ Layer transform (animatable)
// Moves, rotates and scales the whole active layer through its transform properties, so the change is
// keyframed (auto-key) and animates over time. Pixels and shapes are never modified.
class LayerTransformTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::LayerTransform; }
    bool busy() const override { return m_h != None; }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        const Geo g = geo();
        if (!g.ok) { emit m_s->message(QObject::tr("This layer is blank on this frame, so there is nothing to grab. For a drawing layer, use Animation > Extend Here (or Ctrl+= / drag the exposure's edge) to show the drawing on more frames.")); return; }
        if (g.layer->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return; }
        m_h = hit(g, e.doc);
        if (m_h == None) return;
        m_g0 = g; m_start = e.doc; m_moved = false;
        m_key = "xform-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_layer = g.layer->id;
    }
    void move(const PointerEvent& e) override {
        if (m_h == None) return;
        apply(e);
    }
    void release(const PointerEvent& e) override {
        if (m_h == None) return;
        apply(e);
        m_h = None;
        m_c->update();
    }
    void cancel() override {
        if (m_h == None) return;
        if (m_moved && m_s->doc()) m_s->doc()->undoStack()->undo();
        m_h = None;
        m_c->update();
    }
    bool keyPress(QKeyEvent* k) override {
        if (k->key() == Qt::Key_Escape && m_h != None) { cancel(); return true; }
        return false;
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        const Geo g = m_h != None ? geoOf(m_s->doc()->findLayer(m_layer)) : geo();
        if (!g.ok) return;
        const QPolygonF v = t.map(g.poly);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 140, 0), 1));
        p.drawPolygon(v);
        for (const QPointF& q : handlePoints(v)) drawSquare(p, q, 4, false);
        const QPointF a = t.map(g.anchorDoc);                         // anchor point: ring with a cross
        p.setPen(QPen(QColor(255, 140, 0), 1.5));
        p.drawEllipse(a, 6, 6);
        p.drawLine(a - QPointF(9, 0), a + QPointF(9, 0));
        p.drawLine(a - QPointF(0, 9), a + QPointF(0, 9));
    }

private:
    enum H { None, Move, Rotate, Anchor, TL, T, TR, R, BR, B, BL, L };
    struct Geo {
        bool ok = false;
        Layer* layer = nullptr;
        QRectF box;                 // layer space
        QTransform m;               // layer space -> document
        QPolygonF poly;             // box in document space (TL, TR, BR, BL)
        QPointF center, anchorDoc;
        LayerXform xf;
    };
    Geo geoOf(Layer* l) const {
        Geo g;
        if (!l || !m_s->doc()) return g;
        const int f = m_s->doc()->currentFrame();
        const QSize cs = m_s->doc()->canvasSize();
        g.box = l->contentBounds(f, cs, m_s->doc());
        if (g.box.isNull() || g.box.isEmpty()) return g;
        g.layer = l;
        g.xf = l->xformAt(f);
        g.center = QPointF(cs.width() / 2.0, cs.height() / 2.0);
        g.m = g.xf.matrix(g.center);
        g.poly = g.m.map(QPolygonF(g.box));
        if (g.poly.size() > 4) g.poly.resize(4);
        g.anchorDoc = g.center + g.xf.anchor + g.xf.position;
        g.ok = true;
        return g;
    }
    Geo geo() const { return geoOf(m_s->activeLayer()); }
    // Corner and edge-middle points in the order TL, T, TR, R, BR, B, BL, L.
    static QVector<QPointF> handlePoints(const QPolygonF& q) {
        QVector<QPointF> v;
        for (int i = 0; i < 4; ++i) { v << q[i]; v << (q[i] + q[(i + 1) % 4]) / 2; }
        return v;
    }
    static QPointF layerHandle(const QRectF& b, H h) {
        switch (h) {
        case TL: return b.topLeft(); case T: return {b.center().x(), b.top()}; case TR: return b.topRight();
        case R: return {b.right(), b.center().y()}; case BR: return b.bottomRight();
        case B: return {b.center().x(), b.bottom()}; case BL: return b.bottomLeft();
        case L: return {b.left(), b.center().y()}; default: return b.center();
        }
    }
    H hit(const Geo& g, const QPointF& p) const {
        const double tol = 7.0 / m_c->zoom();
        if (QLineF(p, g.anchorDoc).length() <= tol + 2 / m_c->zoom()) return Anchor;
        static const H order[8] = {TL, T, TR, R, BR, B, BL, L};
        const QVector<QPointF> pts = handlePoints(g.poly);
        for (int i : {0, 2, 4, 6, 1, 3, 5, 7}) if (QLineF(p, pts[i]).length() <= tol) return order[i];
        if (g.poly.containsPoint(p, Qt::OddEvenFill)) return Move;
        const double rz = 26.0 / m_c->zoom();
        if (g.poly.boundingRect().adjusted(-rz, -rz, rz, rz).contains(p)) return Rotate;
        return None;
    }
    void apply(const PointerEvent& e) {
        Layer* l = m_s->doc() ? m_s->doc()->findLayer(m_layer) : nullptr;
        if (!l) return;
        const Geo& g = m_g0;
        QVector<QPair<QString, PropValue>> vals;
        auto vec = [](const QString& id, double x, double y) { return qMakePair(id, PropValue::of(x, y)); };
        if (m_h == Move) {
            QPointF d = e.doc - m_start;
            if (e.shift()) { if (std::abs(d.x()) > std::abs(d.y())) d.setY(0); else d.setX(0); }
            vals << vec("transform.position", g.xf.position.x() + d.x(), g.xf.position.y() + d.y());
        } else if (m_h == Rotate) {
            const QPointF a = g.anchorDoc;
            double deg = qRadiansToDegrees(std::atan2(e.doc.y() - a.y(), e.doc.x() - a.x()) - std::atan2(m_start.y() - a.y(), m_start.x() - a.x()));
            double rot = g.xf.rotation + deg;
            if (e.shift()) rot = std::round(rot / 15.0) * 15.0;
            vals << qMakePair(QString("transform.rotation"), PropValue::of(rot));
        } else if (m_h == Anchor) {
            // Move the pivot without moving the picture: compensate with position so the matrix is unchanged.
            const QPointF newA = e.doc - g.center;
            const QPointF keep = g.m.map(e.doc);
            const QPointF pos = keep - e.doc;                       // new anchor lands where the old matrix put that point
            vals << vec("transform.anchor", newA.x(), newA.y()) << vec("transform.position", pos.x(), pos.y());
        } else {
            const QPointF hl = layerHandle(g.box, m_h);
            const QPointF al = g.center + g.xf.anchor;                // anchor in layer space
            QTransform rotInv; rotInv.rotate(-g.xf.rotation);
            const QPointF v = rotInv.map(e.doc - g.anchorDoc);        // pointer relative to the anchor, un-rotated
            double sx = g.xf.scale.x(), sy = g.xf.scale.y();
            const bool horiz = m_h != T && m_h != B, vert = m_h != L && m_h != R;
            if (horiz && std::abs(hl.x() - al.x()) > 1e-6) sx = v.x() / (hl.x() - al.x());
            if (vert && std::abs(hl.y() - al.y()) > 1e-6) sy = v.y() / (hl.y() - al.y());
            if (e.shift()) {
                const double r0x = horiz ? sx / (g.xf.scale.x() == 0 ? 1 : g.xf.scale.x()) : 1, r0y = vert ? sy / (g.xf.scale.y() == 0 ? 1 : g.xf.scale.y()) : 1;
                const double f = (horiz && vert) ? (std::abs(r0x - 1) > std::abs(r0y - 1) ? r0x : r0y) : (horiz ? r0x : r0y);
                sx = g.xf.scale.x() * f; sy = g.xf.scale.y() * f;
            }
            auto fix = [](double s) { return std::abs(s) < 0.01 ? (s < 0 ? -0.01 : 0.01) : s; };
            vals << vec("transform.scale", fix(sx) * 100.0, fix(sy) * 100.0);
        }
        if (m_s->setProperties(m_layer, vals, m_key)) m_moved = true;
        m_c->update();
    }
public:
    bool insideLayer(const QPointF& docPt) const {
        const Geo g = geo();
        return g.ok && g.poly.containsPoint(docPt, Qt::OddEvenFill);
    }
private:
    H m_h = None;
    Geo m_g0;
    QPointF m_start;
    QString m_key;
    QUuid m_layer;
    bool m_moved = false;
};

// ============================================================ Unified Select (V)
// One selection tool that covers three jobs, chosen by what is active and the modifier held:
//  - drawing / raster / group layers: transforms the whole layer (keyframeable);
//  - vector and text layers: selects and moves shapes; Alt+drag transforms the whole layer instead;
//  - Ctrl+drag (on a vector layer): edits points and handles, like Direct Selection.
class UnifiedSelectTool : public Tool {
public:
    UnifiedSelectTool(EditorSession* s, CanvasView* c)
        : Tool(s, c), m_select(s, c), m_direct(s, c), m_layer(s, c) {}
    ToolId id() const override { return ToolId::Select; }
    bool busy() const override { return m_pathDrag != PathNone || (m_active && m_active->busy()); }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        const Layer* l = m_s->activeLayer();
        const bool shapes = isShapeLayer(l);
        m_mapped = false;
        if (e.button == Qt::LeftButton && !e.alt() && !e.ctrl() && pathPress(e)) { m_active = nullptr; return; }
        if (!shapes) m_active = &m_layer;
        else if (e.alt()) m_active = &m_layer;
        else if (e.ctrl() && l->type == LayerType::Vector) { m_active = &m_direct; m_mapped = true; selectShapeUnderPoint(e); }
        else if (l->type == LayerType::Vector && onSelectedPoint(e)) { m_active = &m_direct; m_mapped = true; }   // a visible point is grabbed directly
        else { m_active = &m_select; m_mapped = true; }
        m_active->press(m_mapped ? local(e) : e);
    }
    void move(const PointerEvent& e) override {
        if (m_pathDrag != PathNone) { pathDrag(e); return; }
        if (m_active) m_active->move(m_mapped ? local(e) : e);
    }
    void release(const PointerEvent& e) override {
        if (m_pathDrag != PathNone) { pathDrag(e); m_pathDrag = PathNone; m_c->update(); return; }
        if (m_active) m_active->release(m_mapped ? local(e) : e);
    }
    void doubleClick(const PointerEvent& e) override {
        const Layer* l = m_s->activeLayer();
        if (l && l->type == LayerType::Symbol && m_layer.insideLayer(e.doc) && m_s->doc()) {   // dive into the symbol
            m_s->doc()->enterSymbol(l->symbolId);
            return;
        }
        // Double-clicking a path with Select edits its points (same gesture as the old Direct Selection tool).
        Tool* t = (isShapeLayer(l) && l->type == LayerType::Vector) ? static_cast<Tool*>(&m_direct) : m_active;
        if (t) t->doubleClick(t == &m_layer ? e : local(e));
    }
    void cancel() override {
        if (m_pathDrag != PathNone) {
            if (m_pathMoved && m_s->doc()) m_s->doc()->undoStack()->undo();
            m_pathDrag = PathNone;
            return;
        }
        if (m_active) m_active->cancel();
    }
    bool keyPress(QKeyEvent* k) override {
        if (k->key() == Qt::Key_Escape && m_pathDrag != PathNone) { cancel(); return true; }
        if (m_active && m_active->busy()) return m_active->keyPress(k);
        const Layer* l = m_s->activeLayer();
        if (!isShapeLayer(l)) return m_layer.keyPress(k);
        if (m_direct.keyPress(k)) return true;       // Delete removes the selected point (or the shape)
        return m_select.keyPress(k);
    }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        paintPath(p, t);
        const Layer* l = m_s->activeLayer();
        if (!isShapeLayer(l)) { m_layer.paintOverlay(p, t); return; }
        const QTransform lt = layerMatrix(l) * t;
        if (m_active && m_active->busy()) { (m_active == &m_layer ? m_layer.paintOverlay(p, t) : m_active->paintOverlay(p, lt)); return; }
        m_select.paintOverlay(p, lt);
        const bool editing = l->type == LayerType::Vector && (QGuiApplication::keyboardModifiers() & Qt::ControlModifier);
        if (!editing) {
            // The anchor points of the selected shapes are always visible (grab one to move it; Ctrl shows every shape's points).
            if (l->type != LayerType::Vector) return;
            p.setRenderHint(QPainter::Antialiasing);
            for (const VectorShape& sh : l->shapes) {
                if (!m_s->selectedShapes().contains(sh.id)) continue;
                p.setPen(QPen(QColor(20, 120, 255), 1));
                p.setBrush(Qt::NoBrush);
                p.drawPath(lt.map(sh.path()));
                for (const PathNode& n : sh.nodes) drawSquare(p, lt.map(n.pos), 4, false);
            }
            return;
        }
        // Holding Ctrl: show the points of every shape (the selected one in full, with its handles).
        p.setRenderHint(QPainter::Antialiasing);
        for (const VectorShape& sh : l->shapes) {
            if (m_s->selectedShapes().contains(sh.id)) continue;
            p.setPen(QPen(QColor(20, 120, 255, 140), 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(lt.map(sh.path()));
            for (const PathNode& n : sh.nodes) drawSquare(p, lt.map(n.pos), 4, false);
        }
        m_direct.paintOverlay(p, lt);
    }

private:
    // ---- motion path: the Position keyframes of the active layer drawn on the canvas ----
    enum PathDrag { PathNone, PathKey, PathIn, PathOut };
    struct PathGeo { const Layer* l = nullptr; const PropertyTrack* t = nullptr; QPointF center; };
    PathGeo pathGeo() const {
        PathGeo g;
        const Layer* l = m_s->activeLayer();
        if (!l || !m_s->doc()) return g;
        auto it = l->tracks.constFind("transform.position");
        if (it == l->tracks.constEnd() || it->keys.size() < 2) return g;
        const QSize cs = m_s->doc()->canvasSize();
        g.l = l; g.t = &*it; g.center = QPointF(cs.width() / 2.0, cs.height() / 2.0);
        return g;
    }
    QPointF anchorAt(const PathGeo& g, double frame) const {
        const LayerXform x = g.l->xformAt(frame);
        return g.center + x.anchor;
    }
    QPointF keyPoint(const PathGeo& g, const Keyframe& k) const { return anchorAt(g, k.frame) + QPointF(k.value.c[0], k.value.c[1]); }
    // Handle positions (doc space) of key i; a side with no tangent shows the straight-line default.
    bool handlePoint(const PathGeo& g, int i, bool out, QPointF* pt) const {
        const auto& ks = g.t->keys;
        const int j = out ? i + 1 : i - 1;
        if (j < 0 || j >= ks.size()) return false;
        const QPointF kp = keyPoint(g, ks[i]);
        const QPointF off = out ? ks[i].spatialOut : ks[i].spatialIn;
        if (!off.isNull()) { *pt = kp + off; return true; }
        *pt = kp + (QPointF(ks[j].value.c[0], ks[j].value.c[1]) - QPointF(ks[i].value.c[0], ks[i].value.c[1])) / 3.0;
        return true;
    }
    bool pathPress(const PointerEvent& e) {
        const PathGeo g = pathGeo();
        if (!g.l) return false;
        const double tol = kHitPx / m_c->zoom();
        const auto& ks = g.t->keys;
        int hitKey = -1, hitSide = 0;
        for (int i = 0; i < ks.size() && hitKey < 0; ++i) {      // handles of selected keys sit on top
            if (!m_s->isKeySelected(g.l->id, "transform.position", ks[i].frame)) continue;
            for (bool out : {true, false}) {
                QPointF hp;
                if (handlePoint(g, i, out, &hp) && QLineF(e.doc, hp).length() <= tol) { hitKey = i; hitSide = out ? 1 : -1; break; }
            }
        }
        if (hitKey < 0)
            for (int i = 0; i < ks.size(); ++i)
                if (QLineF(e.doc, keyPoint(g, ks[i])).length() <= tol) { hitKey = i; hitSide = 0; break; }
        if (hitKey < 0) return false;
        if (g.l->props.locked) { emit m_s->message(QObject::tr("The active layer is locked.")); return true; }
        if (hitSide == 0) m_s->setKeySelection({KeyRef{g.l->id, "transform.position", ks[hitKey].frame}});
        m_pathDrag = hitSide == 0 ? PathKey : hitSide > 0 ? PathOut : PathIn;
        m_pathFrame = ks[hitKey].frame;
        m_pathMoved = false;
        m_pathMerge = "path-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pathOrig = g.l->tracks;
        m_pathPress = e.doc;
        return true;
    }
    void pathDrag(const PointerEvent& e) {
        Layer* l = m_s->activeLayer();
        if (!l || !m_pathOrig.contains("transform.position")) return;
        if (!m_pathMoved && QLineF(e.doc, m_pathPress).length() < 3.0 / m_c->zoom()) return;   // a plain click only selects
        PropertyTracks nt = m_pathOrig;
        PropertyTrack& t = nt["transform.position"];
        const int i = t.indexOfKey(m_pathFrame);
        if (i < 0) return;
        const QSize cs = m_s->doc()->canvasSize();
        const QPointF center(cs.width() / 2.0, cs.height() / 2.0);
        const QPointF anchor = center + l->xformAt(m_pathFrame).anchor;
        QPointF pt = e.doc;
        if (m_pathDrag == PathKey) {
            if (m_s->snapEnabled) pt = m_c->snap(pt);
            const PropertyDef* d = findPropertyDef("transform.position");
            t.keys[i].value = clampValue(*d, PropValue::of(pt.x() - anchor.x(), pt.y() - anchor.y()));
        } else {
            const QPointF kp = anchor + QPointF(t.keys[i].value.c[0], t.keys[i].value.c[1]);
            // Moving one handle of a smooth key mirrors the other (Alt breaks the tie).
            (m_pathDrag == PathOut ? t.keys[i].spatialOut : t.keys[i].spatialIn) = pt - kp;
            if (!e.alt()) (m_pathDrag == PathOut ? t.keys[i].spatialIn : t.keys[i].spatialOut) = -(pt - kp);
        }
        if (nt == l->tracks) return;
        m_s->doc()->editTracks(l->id, nt, m_pathDrag == PathKey ? QObject::tr("Move Keyframe") : QObject::tr("Edit Motion Path"), m_pathMerge);
        m_pathMoved = true;
    }
    void paintPath(QPainter& p, const QTransform& t) {
        const PathGeo g = pathGeo();
        if (!g.l) return;
        const auto& ks = g.t->keys;
        const PropertyDef* d = findPropertyDef("transform.position");
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const int f0 = ks.first().frame, f1 = ks.last().frame;
        for (int f = f0; f <= f1; ++f) {
            const PropValue v = g.t->valueAt(*d, f);
            const QPointF q = t.map(anchorAt(g, f) + QPointF(v.c[0], v.c[1]));
            if (f == f0) path.moveTo(q); else path.lineTo(q);
        }
        p.setPen(QPen(QColor(255, 200, 60, 200), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 200, 60, 150));
        for (int f = f0; f <= f1; ++f) {                       // one dot per frame: spacing shows the speed
            const PropValue v = g.t->valueAt(*d, f);
            p.drawEllipse(t.map(anchorAt(g, f) + QPointF(v.c[0], v.c[1])), 1.6, 1.6);
        }
        for (int i = 0; i < ks.size(); ++i) {
            const bool sel = m_s->isKeySelected(g.l->id, "transform.position", ks[i].frame);
            const QPointF kq = t.map(keyPoint(g, ks[i]));
            if (sel) {
                for (bool out : {true, false}) {
                    QPointF hp;
                    if (!handlePoint(g, i, out, &hp)) continue;
                    const QPointF hq = t.map(hp);
                    p.setPen(QPen(QColor(220, 220, 225), 1));
                    p.drawLine(kq, hq);
                    p.setBrush(Qt::white);
                    p.drawEllipse(hq, 4, 4);
                }
            }
            p.setPen(QPen(QColor(20, 20, 20), 1));
            p.setBrush(sel ? QColor(255, 200, 58) : QColor(240, 150, 40));
            p.drawRect(QRectF(kq.x() - 4, kq.y() - 4, 8, 8));
        }
        p.restore();
    }
    PathDrag m_pathDrag = PathNone;
    QPointF m_pathPress;
    int m_pathFrame = 0;
    bool m_pathMoved = false;
    QString m_pathMerge;
    PropertyTracks m_pathOrig;

    static bool isShapeLayer(const Layer* l) { return l && (l->type == LayerType::Vector || l->type == LayerType::Text); }
    QTransform layerMatrix(const Layer* l) const {
        if (!l || !m_s->doc()) return {};
        const QSize cs = m_s->doc()->canvasSize();
        return l->xformAt(m_s->doc()->currentFrame()).matrix(QPointF(cs.width() / 2.0, cs.height() / 2.0));
    }
    // The shape tools work in the layer's own space; the layer may have been moved/rotated/scaled since.
    PointerEvent local(const PointerEvent& e) const {
        PointerEvent r = e;
        bool ok = false;
        const QTransform inv = layerMatrix(m_s->activeLayer()).inverted(&ok);
        if (ok) r.doc = inv.map(e.doc);
        return r;
    }
    bool onSelectedPoint(const PointerEvent& e) const {
        const Layer* l = m_s->activeLayer();
        if (!l || l->props.locked || m_s->selectedShapes().isEmpty()) return false;
        const QPointF q = local(e).doc;
        const double tol = kHitPx / m_c->zoom();
        for (const VectorShape& sh : l->shapes) {
            if (!m_s->selectedShapes().contains(sh.id)) continue;
            for (const PathNode& n : sh.nodes) if (QLineF(q, n.pos).length() <= tol) return true;
        }
        return false;
    }
    // Ctrl-click on a point of a shape that is not selected yet: select that shape so the click grabs the point.
    void selectShapeUnderPoint(const PointerEvent& e) {
        Layer* l = m_s->activeLayer();
        if (!l) return;
        const QPointF q = local(e).doc;
        const double tol = kHitPx / m_c->zoom();
        for (const VectorShape& sh : l->shapes) {
            if (m_s->selectedShapes().contains(sh.id)) {
                for (const PathNode& n : sh.nodes) if (QLineF(q, n.pos).length() <= tol) return;   // already the right one
            }
        }
        for (int i = int(l->shapes.size()) - 1; i >= 0; --i)
            for (const PathNode& n : l->shapes[i].nodes)
                if (QLineF(q, n.pos).length() <= tol) { m_s->setSelectedShapes({l->shapes[i].id}); return; }
    }
    SelectTool m_select;
    DirectSelectTool m_direct;
    LayerTransformTool m_layer;
    Tool* m_active = nullptr;
    bool m_mapped = false;
};

}  // namespace


// ============================================================ Mask editor
static QTransform activeLayerMatrix(EditorSession* s) {
    Layer* l = s->activeLayer();
    if (!l || !s->doc()) return {};
    const QSize cs = s->doc()->canvasSize();
    return l->xformAt(s->doc()->currentFrame()).matrix(QPointF(cs.width() / 2.0, cs.height() / 2.0));
}

void paintMaskOutlines(QPainter& p, EditorSession* s, const QTransform& docToView, int selMask, int selNode) {
    Layer* l = s->activeLayer();
    if (!l || !s->doc() || l->fx.masks.isEmpty()) return;
    const QTransform t = activeLayerMatrix(s) * docToView;
    p.setRenderHint(QPainter::Antialiasing);
    const LayerFx fx = l->fxAt(s->doc()->currentFrame());
    for (int i = 0; i < fx.masks.size(); ++i) {
        const Mask& m = l->fx.masks[i];
        QPainterPath path = t.map(m.shape.path());
        const bool sel = i == selMask;
        const QColor c = m.mode == MaskMode::Subtract ? QColor(255, 90, 90) : m.mode == MaskMode::Intersect ? QColor(90, 170, 255) : QColor(255, 220, 60);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 160), sel ? 3.0 : 2.0)); p.drawPath(path);
        p.setPen(QPen(c, sel ? 1.6 : 1.0)); p.drawPath(path);
        if (!sel) continue;
        for (int k = 0; k < m.shape.nodes.size(); ++k) {
            const PathNode& n = m.shape.nodes[k];
            const QPointF a = t.map(n.pos);
            if (k == selNode) {
                p.setPen(QPen(QColor(120, 120, 120), 1));
                for (const QPointF& h : {n.in, n.out}) if (!h.isNull()) {
                    const QPointF hp = t.map(n.pos + h);
                    p.drawLine(a, hp); p.setBrush(Qt::white); p.drawEllipse(hp, 3.0, 3.0);
                }
            }
            p.setPen(QPen(Qt::black, 1)); p.setBrush(k == selNode ? c : Qt::white);
            p.drawRect(QRectF(a.x() - 3.5, a.y() - 3.5, 7, 7));
        }
    }
}

class MaskTool : public Tool {
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Mask; }
    bool busy() const override { return m_drag != None; }
    QCursor cursor() const override { return Qt::ArrowCursor; }

    void press(const PointerEvent& e) override {
        if (e.button != Qt::LeftButton) return;
        Layer* l = m_s->activeLayer();
        if (!l) { emit m_s->message(QObject::tr("Select a layer to mask.")); return; }
        if (l->props.locked) return;
        const QPointF q = toLocal(e.doc);
        const double tol = kHitPx / m_c->zoom();
        m_before = l->fx;
        if (m_drawing) return drawPress(*l, q, tol);
        if (valid(*l)) {
            const Mask& m = l->fx.masks[m_mask];
            if (m_node >= 0 && m_node < m.shape.nodes.size()) {
                const PathNode& n = m.shape.nodes[m_node];
                if (!n.out.isNull() && QLineF(q, n.pos + n.out).length() <= tol) return begin(HandleOut);
                if (!n.in.isNull() && QLineF(q, n.pos + n.in).length() <= tol) return begin(HandleIn);
            }
            for (int i = 0; i < m.shape.nodes.size(); ++i)
                if (QLineF(q, m.shape.nodes[i].pos).length() <= tol) { m_node = i; m_c->update(); return begin(Node); }
            const VectorShape::SegmentHit sh = m.shape.nearestSegment(q);
            if (sh.seg >= 0 && sh.distance <= tol) {
                m_seg = sh.seg; m_segT = std::clamp(sh.t, 0.15, 0.85); m_segPress = sh.point;
                const int j = (sh.seg + 1) % m.shape.nodes.size();
                m_segOut = m.shape.nodes[sh.seg].out; m_segIn = m.shape.nodes[j].in;
                m_node = -1;
                return begin(Segment);
            }
        }
        // pick another mask: its outline, or inside it (topmost = last)
        int pick = -1;
        for (int i = l->fx.masks.size() - 1; i >= 0 && pick < 0; --i) {
            const VectorShape& sh = l->fx.masks[i].shape;
            const VectorShape::SegmentHit h = sh.nearestSegment(q);
            if ((h.seg >= 0 && h.distance <= tol) || sh.path().contains(q)) pick = i;
        }
        if (pick >= 0) { m_mask = pick; m_node = -1; m_c->update(); return; }
        // empty area: start drawing a new mask by clicking its points (After Effects style)
        m_mask = -1; m_node = -1;
        m_drawing = true; m_draw.clear();
        drawPress(*l, q, tol);
    }
    void move(const PointerEvent& e) override {
        if (m_drag == DrawHandle) {
            if (m_draw.isEmpty()) return;
            PathNode& n = m_draw.last();
            const QPointF off = toLocal(e.doc) - n.pos;
            if (QLineF(QPointF(0, 0), off).length() * m_c->zoom() > 3) { n.out = off; n.in = -off; n.smooth = true; }
            m_c->update();
            return;
        }
        if (m_drag == None) return;
        Layer* l = m_s->activeLayer();
        if (!l || !valid(*l)) return;
        VectorShape& sh = l->fx.masks[m_mask].shape;
        const QPointF q = toLocal(e.doc);
        if (m_drag == Segment) {
            const int n = sh.nodes.size(), j = (m_seg + 1) % n;
            const QPointF d = (q - m_segPress) / (3.0 * m_segT * (1.0 - m_segT));
            PathNode& a = sh.nodes[m_seg]; PathNode& b = sh.nodes[j];
            a.out = m_segOut + d; b.in = m_segIn + d;
            if (a.smooth) a.in = -a.out;
            if (b.smooth) b.out = -b.in;
        } else if (m_node >= 0 && m_node < sh.nodes.size()) {
            PathNode& n = sh.nodes[m_node];
            if (m_drag == Node) n.pos = q;
            else {
                const QPointF off = q - n.pos;
                if (m_drag == HandleOut) { n.out = off; if (n.smooth && !e.alt()) n.in = -off; }
                else { n.in = off; if (n.smooth && !e.alt()) n.out = -off; }
            }
        }
        m_s->doc()->emitContentChanged({});
    }
    void release(const PointerEvent&) override {
        if (m_drag == DrawHandle) { m_drag = None; m_c->update(); return; }
        finish(QObject::tr("Edit Mask"));
    }
    void doubleClick(const PointerEvent& e) override {
        Layer* l = m_s->activeLayer();
        if (m_drawing) {                                       // the second click of a double-click already added a duplicate point
            if (m_draw.size() > 1 && QLineF(m_draw.last().pos, m_draw[m_draw.size() - 2].pos).length() * m_c->zoom() < 4) m_draw.removeLast();
            if (l) closeDrawing(*l);
            return;
        }
        if (!l || l->props.locked || !valid(*l)) return;
        const QPointF q = toLocal(e.doc);
        const double tol = kHitPx / m_c->zoom();
        m_before = l->fx;
        LayerFx after = l->fx;
        VectorShape& sh = after.masks[m_mask].shape;
        for (int i = 0; i < sh.nodes.size(); ++i) {
            if (QLineF(q, sh.nodes[i].pos).length() > tol) continue;
            PathNode& n = sh.nodes[i];
            if (n.smooth || !n.in.isNull() || !n.out.isNull()) { n.in = n.out = {}; n.smooth = false; }
            else {
                const int cnt = sh.nodes.size();
                const QPointF prev = sh.nodes[(i + cnt - 1) % cnt].pos, next = sh.nodes[(i + 1) % cnt].pos;
                QPointF dir = next - prev; const double len = std::hypot(dir.x(), dir.y());
                if (len > 0) { dir = dir / len * (std::min(QLineF(prev, n.pos).length(), QLineF(n.pos, next).length()) / 3.0); n.out = dir; n.in = -dir; n.smooth = true; }
            }
            m_node = i;
            m_s->doc()->setLayerFx(l->id, after, QObject::tr("Toggle Smooth Mask Point"));
            return;
        }
        const VectorShape::SegmentHit hit = sh.nearestSegment(q);
        if (hit.seg >= 0 && hit.distance <= tol) {
            const int idx = sh.insertNode(hit.seg, hit.t);
            if (idx >= 0) { m_node = idx; m_s->doc()->setLayerFx(l->id, after, QObject::tr("Add Mask Point")); }
        }
    }
    bool keyPress(QKeyEvent* k) override {
        if (m_drawing) {
            Layer* dl = m_s->activeLayer();
            if (k->key() == Qt::Key_Escape) { m_drawing = false; m_draw.clear(); m_c->update(); return true; }
            if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && dl) { closeDrawing(*dl); return true; }
            if ((k->key() == Qt::Key_Backspace || k->key() == Qt::Key_Delete) && !m_draw.isEmpty()) { m_draw.removeLast(); if (m_draw.isEmpty()) m_drawing = false; m_c->update(); return true; }
            return false;
        }
        if (k->key() != Qt::Key_Delete && k->key() != Qt::Key_Backspace) return false;
        Layer* l = m_s->activeLayer();
        if (!l || l->props.locked || !valid(*l)) return false;
        LayerFx after = l->fx;
        VectorShape& sh = after.masks[m_mask].shape;
        if (m_node >= 0 && m_node < sh.nodes.size() && sh.nodes.size() > 3) { sh.nodes.removeAt(m_node); m_node = -1; m_s->doc()->setLayerFx(l->id, after, QObject::tr("Delete Mask Point")); }
        else if (m_node < 0) {                                  // no point selected: delete the whole mask (and its keyframes)
            const QString idStr = after.masks[m_mask].id.toString(QUuid::WithoutBraces);
            after.masks.removeAt(m_mask);
            PropertyTracks pruned = l->tracks; bool orphans = false;
            for (const QString& key : l->tracks.keys()) if (isFxPropId(key) && key.contains(idStr)) { pruned.remove(key); orphans = true; }
            if (orphans) { m_s->doc()->undoStack()->beginMacro(QObject::tr("Delete Mask")); m_s->doc()->setLayerFx(l->id, after, QObject::tr("Delete Mask")); m_s->doc()->editTracks(l->id, pruned, QObject::tr("Delete Mask")); m_s->doc()->undoStack()->endMacro(); }
            else m_s->doc()->setLayerFx(l->id, after, QObject::tr("Delete Mask"));
            m_mask = -1;
        } else return false;
        m_c->update();
        return true;
    }
    void cancel() override { restore(); m_drawing = false; m_draw.clear(); }
    void paintOverlay(QPainter& p, const QTransform& t) override {
        paintMaskOutlines(p, m_s, t, m_mask, m_node);
        if (!m_drawing || m_draw.isEmpty()) return;
        VectorShape sh; sh.nodes = m_draw; sh.closed = false;
        const QTransform lt = activeLayerMatrix(m_s) * t;
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 160), 3)); p.drawPath(lt.map(sh.path()));
        p.setPen(QPen(QColor(255, 220, 60), 1.4)); p.drawPath(lt.map(sh.path()));
        for (int i = 0; i < m_draw.size(); ++i) {
            const QPointF a = lt.map(m_draw[i].pos);
            p.setPen(QPen(Qt::black, 1)); p.setBrush(i == 0 && m_draw.size() >= 3 ? QColor(255, 220, 60) : Qt::white);
            p.drawRect(QRectF(a.x() - 3.5, a.y() - 3.5, 7, 7));
        }
    }
    bool drawing() const { return m_drawing; }

    int selectedMask() const { return m_mask; }

private:
    enum Drag { None, Node, HandleIn, HandleOut, Segment, DrawHandle };
    bool m_drawing = false;
    QVector<PathNode> m_draw;                                   // the mask being drawn, in layer space
    Drag m_drag = None;
    int m_mask = -1, m_node = -1, m_seg = -1;
    double m_segT = 0.5;
    QPointF m_segPress, m_segOut, m_segIn;
    LayerFx m_before;

    QPointF toLocal(const QPointF& doc) const {
        bool ok = false; const QTransform inv = activeLayerMatrix(m_s).inverted(&ok);
        return ok ? inv.map(doc) : doc;
    }
    bool valid(const Layer& l) const { return m_mask >= 0 && m_mask < l.fx.masks.size(); }
    void begin(Drag d) { m_drag = d; m_c->update(); }
    void drawPress(Layer& l, const QPointF& q, double tol) {
        if (m_draw.size() >= 3 && QLineF(q, m_draw.first().pos).length() <= tol) { closeDrawing(l); return; }     // click on the first point closes the mask
        PathNode n; n.pos = q; m_draw << n;
        m_drag = DrawHandle;                                    // dragging from the new point pulls out smooth handles
        m_c->update();
    }
    void closeDrawing(Layer& l) {
        const bool ok = m_draw.size() >= 3;
        if (ok) {
            LayerFx after = l.fx;
            Mask m; m.shape.nodes = m_draw; m.shape.closed = true; m.shape.fill.color = Qt::white; m.name = QObject::tr("Mask %1").arg(after.masks.size() + 1);
            after.masks << m;
            m_s->doc()->setLayerFx(l.id, after, QObject::tr("Draw Mask"));
            m_mask = after.masks.size() - 1; m_node = -1;
        } else emit m_s->message(QObject::tr("A mask needs at least 3 points."));
        m_drawing = false; m_drag = None; m_draw.clear();
        m_c->update();
    }
    void restore() {
        Layer* l = m_s->activeLayer();
        if (m_drag != None && l) { l->fx = m_before; m_s->doc()->emitContentChanged({}); }
        m_drag = None; m_c->update();
    }
    void finish(const QString& text) {
        if (m_drag == None) return;
        Layer* l = m_s->activeLayer();
        m_drag = None;
        if (l) {
            const LayerFx after = l->fx;
            l->fx = m_before;                                   // the drag edited the layer live; the undo command needs the true "before"
            m_s->doc()->setLayerFx(l->id, after, text);
        }
        m_c->update();
    }
};

std::unique_ptr<Tool> createTool(ToolId id, EditorSession* s, CanvasView* c) {
    switch (id) {
    case ToolId::Select: return std::make_unique<UnifiedSelectTool>(s, c);
    case ToolId::DirectSelect: return std::make_unique<DirectSelectTool>(s, c);
    case ToolId::Brush:
    case ToolId::Pencil:
    case ToolId::Smudge:
    case ToolId::Eraser: return std::make_unique<BrushTool>(id, s, c);
    case ToolId::Pen: return std::make_unique<PenTool>(s, c);
    case ToolId::Line:
    case ToolId::Rectangle:
    case ToolId::Ellipse:
    case ToolId::Polygon: return std::make_unique<ShapeTool>(id, s, c);
    case ToolId::Bucket: return std::make_unique<BucketTool>(s, c);
    case ToolId::Gradient: return std::make_unique<GradientTool>(s, c);
    case ToolId::Eyedropper: return std::make_unique<EyedropperTool>(s, c);
    case ToolId::Hand: return std::make_unique<HandTool>(s, c);
    case ToolId::Zoom: return std::make_unique<ZoomTool>(s, c);
    case ToolId::FreeTransform: return std::make_unique<FreeTransformTool>(s, c);
    case ToolId::Text: return std::make_unique<TextTool>(s, c);
    case ToolId::LayerTransform: return std::make_unique<LayerTransformTool>(s, c);
    case ToolId::Mask: return std::make_unique<MaskTool>(s, c);
    }
    return nullptr;
}

}  // namespace mynd
