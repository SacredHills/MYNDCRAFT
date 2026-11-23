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
        BrushSettings b = m_s->brush(m_id);
        const bool erase = m_id == ToolId::Eraser || e.eraserEnd;
        const BrushMode mode = m_id == ToolId::Smudge ? BrushMode::Smudge : erase ? BrushMode::Erase : BrushMode::Paint;
        m_stroke = std::make_unique<BrushStroke>(&l->raster, b, m_s->foreground(), mode);
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
        if (Layer* l = m_s->doc()->findLayer(m_layer)) l->raster = m_stroke->original().copy();
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
    QUuid m_layer;
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
        if (l->type == LayerType::Raster) {
            const QPoint p = e.doc.toPoint();
            const QImage before = l->raster.copy();
            const QRect r = floodFill(l->raster, p, m_s->foreground(), m_s->bucketTolerance);
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
        if (l->type == LayerType::Raster) {
            const QImage before = l->raster.copy();
            QLinearGradient g(m_a, m_b);
            g.setColorAt(0, m_s->foreground());
            g.setColorAt(1, m_s->background());
            QPainter p(&l->raster);
            p.fillRect(l->raster.rect(), g);
            p.end();
            m_s->doc()->commitRasterPatch(l->id, l->raster.rect(), before, QObject::tr("Gradient Fill"));
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
    enum Drag { None, Node, HandleIn, HandleOut };
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
        else m_orig = l->raster.copy();
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
            } else if (!m_t.isIdentity()) {
                m_s->doc()->commitRasterPatch(l->id, l->raster.rect(), m_orig, QObject::tr("Transform Layer"));
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
            l->raster.fill(Qt::transparent);
            QPainter p(&l->raster);
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
        if (l->type == LayerType::Raster && !l->raster.isNull()) {
            const int idx = m_s->doc()->undoStack()->index();
            if (m_cacheLayer != l->id || m_cacheIndex != idx) { m_cacheBox = contentBounds(l->raster); m_cacheLayer = l->id; m_cacheIndex = idx; }
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
    QRect m_cacheBox; QUuid m_cacheLayer; int m_cacheIndex = -2;
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

}  // namespace

std::unique_ptr<Tool> createTool(ToolId id, EditorSession* s, CanvasView* c) {
    switch (id) {
    case ToolId::Select: return std::make_unique<SelectTool>(s, c);
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
    }
    return nullptr;
}

}  // namespace mynd
