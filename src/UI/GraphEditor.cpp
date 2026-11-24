#include "UI/GraphEditor.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

namespace mynd {

namespace {
const QColor kComp[4] = {QColor(0xe0, 0x5a, 0x4f), QColor(0x5a, 0xc0, 0x6a), QColor(0x4f, 0x9a, 0xe0), QColor(0xd0, 0xb0, 0x40)};
constexpr double kHit = 7.0;

double niceStep(double raw) {
    if (raw <= 0) return 1;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / p;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * p;
}
}  // namespace

GraphCanvas::GraphCanvas(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    setMinimumHeight(110);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    auto hook = [this] { update(); };
    connect(s, &EditorSession::keySelectionChanged, this, hook);
    connect(s, &EditorSession::activeLayerChanged, this, [this] { update(); });
}

const Layer* GraphCanvas::layer() const { return m_s->activeLayer(); }

const PropertyTrack* GraphCanvas::track() const {
    const Layer* l = layer();
    if (!l) return nullptr;
    auto it = l->tracks.constFind(m_prop);
    return it == l->tracks.constEnd() || !it->animated() ? nullptr : &*it;
}

void GraphCanvas::setProperty(const QString& id) {
    m_prop = id;
    fit();
}

int GraphCanvas::primaryComp(const Keyframe& a, const Keyframe& b) const {
    const PropertyDef* d = findPropertyDef(m_prop);
    if (!d) return -1;
    int best = -1; double mx = 1e-9;
    for (int c = 0; c < propComponents(d->type); ++c) {
        const double dv = std::abs(b.value.c[c] - a.value.c[c]);
        if (dv > mx) { mx = dv; best = c; }
    }
    return best;
}

void GraphCanvas::layoutMetrics() { m_bottomY = height() - 18; }

void GraphCanvas::fit() {
    layoutMetrics();
    const PropertyTrack* t = track();
    const PropertyDef* d = findPropertyDef(m_prop);
    if (!t || !d) { m_f0 = 0; m_fs = 10; m_v0 = 0; m_vs = 1; update(); return; }
    double f0 = t->keys.first().frame, f1 = t->keys.last().frame;
    if (f1 - f0 < 1) f1 = f0 + 1;
    double lo = 1e300, hi = -1e300;
    if (m_mode == Mode::Speed) {
        lo = 0;
        for (double f = f0; f <= f1; f += std::max(0.25, (f1 - f0) / 200)) hi = std::max(hi, t->speedAt(*d, f));
    } else {
        for (const Keyframe& k : t->keys)
            for (int c = 0; c < propComponents(d->type); ++c) { lo = std::min(lo, k.value.c[c]); hi = std::max(hi, k.value.c[c]); }
        // overshooting easing can leave the key range
        for (double f = f0; f <= f1; f += std::max(0.25, (f1 - f0) / 200)) {
            const PropValue v = t->valueAt(*d, f);
            for (int c = 0; c < propComponents(d->type); ++c) { lo = std::min(lo, v.c[c]); hi = std::max(hi, v.c[c]); }
        }
    }
    if (hi - lo < 1e-6) { lo -= 1; hi += 1; }
    const double pad = (hi - lo) * 0.15;
    lo -= (m_mode == Mode::Speed ? 0 : pad); hi += pad;
    const double w = std::max(50, width() - m_left - 16), h = std::max(40, m_bottomY - m_top - 4);
    const double fpad = (f1 - f0) * 0.06;
    m_f0 = f0 - fpad;
    m_fs = w / ((f1 - f0) + 2 * fpad);
    m_v0 = lo;
    m_vs = h / (hi - lo);
    update();
}

bool GraphCanvas::handleGeometry(int i, int side, QPointF* out) const {
    const PropertyTrack* t = track();
    if (!t || i < 0 || i >= t->keys.size()) return false;
    const int j = side > 0 ? i + 1 : i - 1;
    if (j < 0 || j >= t->keys.size()) return false;
    const Keyframe& a = side > 0 ? t->keys[i] : t->keys[j];
    const Keyframe& b = side > 0 ? t->keys[j] : t->keys[i];
    const int c = primaryComp(a, b);
    if (c < 0) return false;
    const double dur = b.frame - a.frame, dv = b.value.c[c] - a.value.c[c];
    if (side > 0) {
        const Handle h = a.out == Interp::Bezier ? a.outHandle : Handle{};
        *out = toView(a.frame + h.x * dur, a.value.c[c] + h.y * dv);
    } else {
        const Handle h = b.in == Interp::Bezier ? b.inHandle : Handle{};
        *out = toView(b.frame - h.x * dur, b.value.c[c] - h.y * dv);
    }
    return true;
}

QPointF GraphCanvas::keyPoint(int frame, int comp) const {
    const PropertyTrack* t = track();
    if (!t) return {};
    const int i = t->indexOfKey(frame);
    if (i < 0) return {};
    const PropertyDef* d = findPropertyDef(m_prop);
    return toView(frame, m_mode == Mode::Speed ? t->speedAt(*d, frame) : t->keys[i].value.c[comp]);
}

bool GraphCanvas::handlePoint(int frame, bool outgoing, QPointF* out) const {
    const PropertyTrack* t = track();
    return t && handleGeometry(t->indexOfKey(frame), outgoing ? 1 : -1, out);
}

GraphCanvas::Hit GraphCanvas::hitTest(const QPointF& p) const {
    Hit none;
    const PropertyTrack* t = track();
    const PropertyDef* d = findPropertyDef(m_prop);
    const Layer* l = layer();
    if (!t || !d || !l) return none;
    // Handles of the selected keys first (they sit on top of the curves).
    if (m_mode == Mode::Value) {
        for (int i = 0; i < t->keys.size(); ++i) {
            if (!m_s->isKeySelected(l->id, m_prop, t->keys[i].frame)) continue;
            for (int side : {1, -1}) {
                QPointF hp;
                if (handleGeometry(i, side, &hp) && QLineF(p, hp).length() <= kHit) { Hit h; h.key = i; h.handle = side; return h; }
            }
        }
    }
    for (int i = 0; i < t->keys.size(); ++i) {
        for (int c = 0; c < propComponents(d->type); ++c) {
            if (m_mode == Mode::Speed && c > 0) break;
            if (QLineF(p, keyPoint(t->keys[i].frame, c)).length() <= kHit) { Hit h; h.key = i; h.comp = c; return h; }
        }
    }
    return none;
}

void GraphCanvas::paintEvent(QPaintEvent*) {
    layoutMetrics();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x24, 0x24, 0x27));
    const PropertyTrack* t = track();
    const PropertyDef* d = findPropertyDef(m_prop);
    const Layer* l = layer();
    const QRect plot(m_left, m_top, width() - m_left - 8, m_bottomY - m_top);
    p.setPen(QColor(0x88, 0x88, 0x8c));
    if (!t || !d || !l) {
        p.drawText(rect(), Qt::AlignCenter, tr("Animate a property (stopwatch in Properties) to edit its curve here."));
        return;
    }
    p.setClipRect(rect());
    // grid + labels
    QFont f = font(); f.setPointSizeF(f.pointSizeF() - 1); p.setFont(f);
    const double fstep = niceStep(60.0 / m_fs), vstep = niceStep(40.0 / m_vs);
    for (double fr = std::ceil(m_f0 / fstep) * fstep; toView(fr, 0).x() < plot.right(); fr += fstep) {
        const double x = toView(fr, 0).x();
        if (x < plot.left()) continue;
        p.setPen(QColor(0x34, 0x34, 0x38)); p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(QColor(0x99, 0x99, 0x9e)); p.drawText(QPointF(x + 3, plot.top() - 6), QString::number(int(fr) + 1));
    }
    for (double v = std::ceil(m_v0 / vstep) * vstep; toView(0, v).y() > plot.top(); v += vstep) {
        const double y = toView(0, v).y();
        if (y > plot.bottom()) continue;
        p.setPen(QColor(0x34, 0x34, 0x38)); p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(0x99, 0x99, 0x9e)); p.drawText(QRectF(0, y - 8, m_left - 4, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'g', 5));
    }
    p.setClipRect(plot.adjusted(-1, -1, 1, 1));
    // playhead
    if (m_s->doc()) {
        const double x = toView(m_s->doc()->currentFrame(), 0).x();
        p.setPen(QPen(QColor(0xc0, 0x39, 0x2b), 1)); p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    const int n = propComponents(d->type);
    const double f0 = t->keys.first().frame, f1 = t->keys.last().frame;
    const double step = std::max(0.1, 1.5 / m_fs);
    for (int c = 0; c < (m_mode == Mode::Speed ? 1 : n); ++c) {
        QPainterPath path;
        bool first = true;
        for (double fr = f0; fr <= f1 + 1e-9; fr += step) {
            const double ff = std::min(fr, f1);
            const double v = m_mode == Mode::Speed ? t->speedAt(*d, ff) : t->valueAt(*d, ff).c[c];
            const QPointF q = toView(ff, v);
            if (first) { path.moveTo(q); first = false; } else path.lineTo(q);
        }
        p.setPen(QPen(m_mode == Mode::Speed ? QColor(0xe0, 0x9a, 0x3a) : kComp[c], 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // handles of selected keys, then keys
    if (m_mode == Mode::Value) {
        for (int i = 0; i < t->keys.size(); ++i) {
            if (!m_s->isKeySelected(l->id, m_prop, t->keys[i].frame)) continue;
            for (int side : {1, -1}) {
                QPointF hp;
                if (!handleGeometry(i, side, &hp)) continue;
                const int j = i + side;
                const Keyframe& a = side > 0 ? t->keys[i] : t->keys[j];
                const Keyframe& b = side > 0 ? t->keys[j] : t->keys[i];
                const int c = primaryComp(a, b);
                p.setPen(QPen(QColor(0xaa, 0xaa, 0xb0), 1));
                p.drawLine(keyPoint(t->keys[i].frame, c), hp);
                p.setBrush(QColor(0xf0, 0xf0, 0xf0)); p.drawEllipse(hp, 4, 4);
            }
        }
    }
    for (int i = 0; i < t->keys.size(); ++i) {
        const bool sel = m_s->isKeySelected(l->id, m_prop, t->keys[i].frame);
        for (int c = 0; c < (m_mode == Mode::Speed ? 1 : n); ++c) {
            const QPointF q = keyPoint(t->keys[i].frame, c);
            p.setPen(QPen(QColor(0x10, 0x10, 0x10), 1));
            p.setBrush(sel ? QColor(0xff, 0xc8, 0x3a) : (m_mode == Mode::Speed ? QColor(0xe0, 0x9a, 0x3a) : kComp[c]));
            p.drawRect(QRectF(q.x() - 4, q.y() - 4, 8, 8));
        }
    }
    p.setClipping(false);
    p.setPen(QColor(0x99, 0x99, 0x9e));
    QString legend = m_mode == Mode::Speed ? tr("Speed (units per frame)") : d->label;
    if (m_mode == Mode::Value) for (int c = 0; c < n; ++c) legend += QString("   %1").arg(c < d->componentNames.size() ? d->componentNames[c] : QString::number(c + 1));
    p.drawText(QPointF(m_left, height() - 4), legend);
}

void GraphCanvas::mousePressEvent(QMouseEvent* e) {
    const Layer* l = layer();
    if (e->button() == Qt::MiddleButton) { m_panning = true; m_panLast = e->position(); return; }
    if (e->button() != Qt::LeftButton || !l) return;
    setFocus();
    if (e->position().y() < m_top && m_s->doc()) {   // ruler: scrub
        m_s->doc()->setCurrentFrame(std::clamp(int(std::lround(frameAt(e->position().x()))), 0, m_s->doc()->durationFrames() - 1));
        return;
    }
    const PropertyTrack* t = track();
    if (!t) return;
    const Hit h = hitTest(e->position());
    if (h.key < 0) { if (!(e->modifiers() & Qt::ShiftModifier)) m_s->clearKeySelection(); return; }
    if (h.handle == 0) {
        const KeyRef ref{l->id, m_prop, t->keys[h.key].frame};
        QVector<KeyRef> sel = m_s->selectedKeys();
        if (e->modifiers() & Qt::ShiftModifier) { if (sel.contains(ref)) sel.removeOne(ref); else sel.append(ref); }
        else if (!sel.contains(ref)) sel = {ref};
        m_s->setKeySelection(sel);
    }
    if (l->props.locked || (m_mode == Mode::Speed && h.handle == 0 && false)) return;
    m_drag = h; m_dragging = true; m_pressPt = e->position(); m_orig = l->tracks;
    m_mergeKey = "graph-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void GraphCanvas::applyDrag(const QPointF& p) {
    Layer* l = m_s->activeLayer();
    const PropertyDef* d = findPropertyDef(m_prop);
    if (!l || !d || !m_orig.contains(m_prop)) return;
    PropertyTracks nt = m_orig;
    PropertyTrack& t = nt[m_prop];
    const int i = m_drag.key;
    if (i < 0 || i >= t.keys.size()) return;
    Keyframe& k = t.keys[i];
    if (m_drag.handle == 0) {
        if (m_mode == Mode::Speed) return;               // the speed graph is a read-out; edit values/handles in the value graph
        const int lo = i > 0 ? t.keys[i - 1].frame + 1 : 0;
        const int hi = i + 1 < t.keys.size() ? t.keys[i + 1].frame - 1 : 1 << 20;
        const int nf = std::clamp(int(std::lround(frameAt(p.x()))), lo, hi);
        PropValue v = k.value;
        v.c[m_drag.comp] = valueAt(p.y());
        v = clampValue(*d, v);
        // keep the keyframe selection pointing at the moved key
        const int oldFrame = k.frame;
        k.frame = nf; k.value = v;
        t.keys[i] = k;
        if (nf != oldFrame) {
            QVector<KeyRef> sel = m_s->selectedKeys();
            for (KeyRef& r : sel) if (r.layer == l->id && r.prop == m_prop && r.frame == oldFrame) r.frame = nf;
            m_pendingSel = sel; m_hasPendingSel = true;
        }
    } else {
        const bool out = m_drag.handle > 0;
        const int j = out ? i + 1 : i - 1;
        if (j < 0 || j >= t.keys.size()) return;
        Keyframe& a = out ? t.keys[i] : t.keys[j];
        Keyframe& b = out ? t.keys[j] : t.keys[i];
        const int c = primaryComp(a, b);
        if (c < 0) return;
        const double dur = b.frame - a.frame, dv = b.value.c[c] - a.value.c[c];
        const double fr = frameAt(p.x()), val = valueAt(p.y());
        Handle h;
        if (out) { h.x = (fr - a.frame) / dur; h.y = (val - a.value.c[c]) / dv; }
        else { h.x = (b.frame - fr) / dur; h.y = (b.value.c[c] - val) / dv; }
        h.x = std::clamp(h.x, 0.0, 1.0);
        h.y = std::clamp(h.y, -4.0, 5.0);
        if (out) { a.out = Interp::Bezier; a.outHandle = h; } else { b.in = Interp::Bezier; b.inHandle = h; }
    }
    if (nt == l->tracks) return;
    m_s->doc()->editTracks(l->id, nt, m_drag.handle == 0 ? tr("Move Keyframe") : tr("Edit Curve Handle"), m_mergeKey);
    if (m_hasPendingSel) { m_s->setKeySelection(m_pendingSel); m_hasPendingSel = false; }
}

void GraphCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        const QPointF d = e->position() - m_panLast; m_panLast = e->position();
        m_f0 -= d.x() / m_fs; m_v0 += d.y() / m_vs; update();
        return;
    }
    if (m_dragging) applyDrag(e->position());
}

void GraphCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) { m_panning = false; return; }
    if (m_dragging) applyDrag(e->position());
    m_dragging = false;
    m_drag = Hit{};
    update();
}

void GraphCanvas::wheelEvent(QWheelEvent* e) {
    const double z = std::pow(1.0015, e->angleDelta().y());
    if (e->modifiers() & Qt::ShiftModifier) {
        const double v = valueAt(e->position().y());
        m_vs *= z; m_v0 = v - (m_bottomY - e->position().y()) / m_vs;
    } else {
        const double fr = frameAt(e->position().x());
        m_fs *= z; m_f0 = fr - (e->position().x() - m_left) / m_fs;
    }
    update();
    e->accept();
}

// ============================================================ panel
GraphEditorPanel::GraphEditorPanel(EditorSession* s, QWidget* parent) : QWidget(parent), m_s(s) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(2);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_props = new QComboBox;
    m_props->setMinimumWidth(120);
    bar->addWidget(m_props);
    m_canvas = new GraphCanvas(s);
    auto mkBtn = [&](const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        bar->addWidget(b);
        return b;
    };
    auto* valueBtn = mkBtn(tr("Value"), tr("Value graph: edit keys and Bezier handles"), [this] { m_canvas->setMode(GraphCanvas::Mode::Value); m_canvas->fit(); });
    auto* speedBtn = mkBtn(tr("Speed"), tr("Speed graph: how fast the property changes (read-out)"), [this] { m_canvas->setMode(GraphCanvas::Mode::Speed); m_canvas->fit(); });
    for (auto* b : {valueBtn, speedBtn}) b->setCheckable(true);
    valueBtn->setChecked(true);
    connect(valueBtn, &QToolButton::clicked, this, [=] { valueBtn->setChecked(true); speedBtn->setChecked(false); });
    connect(speedBtn, &QToolButton::clicked, this, [=] { speedBtn->setChecked(true); valueBtn->setChecked(false); });
    bar->addSpacing(8);
    mkBtn(tr("Fit"), tr("Fit the curve in the view"), [this] { m_canvas->fit(); });
    bar->addSpacing(8);
    mkBtn(tr("Linear"), tr("Linear interpolation on the selected keys"), [s] { s->easeSelectedKeys(EasePreset::Linear); });
    mkBtn(tr("Hold"), tr("Hold on the selected keys"), [s] { s->easeSelectedKeys(EasePreset::Hold); });
    mkBtn(tr("Ease In"), tr("Ease In (Shift+F9)"), [s] { s->easeSelectedKeys(EasePreset::EaseIn); });
    mkBtn(tr("Ease Out"), tr("Ease Out"), [s] { s->easeSelectedKeys(EasePreset::EaseOut); });
    mkBtn(tr("Easy Ease"), tr("Easy Ease (F9)"), [s] { s->easeSelectedKeys(EasePreset::EaseInOut); });
    bar->addStretch(1);
    v->addLayout(bar);
    v->addWidget(m_canvas, 1);

    connect(m_props, &QComboBox::activated, this, [this] { m_canvas->setProperty(m_props->currentData().toString()); });
    auto refresh = [this] { rebuildProps(); };
    connect(s, &EditorSession::activeLayerChanged, this, refresh);
    connect(s, &EditorSession::keySelectionChanged, this, [this] {
        // selecting keys elsewhere (timeline) shows their property
        const auto sel = m_s->selectedKeys();
        if (!sel.isEmpty() && sel.first().prop != m_canvas->property()) {
            const int ix = m_props->findData(sel.first().prop);
            if (ix >= 0) { m_props->setCurrentIndex(ix); m_canvas->setProperty(sel.first().prop); }
        }
    });
    connect(s, &EditorSession::documentChanged, this, [this, refresh] {
        disconnect(m_c1); disconnect(m_c2);
        if (Document* d = m_s->doc()) {
            m_c1 = connect(d, &Document::tracksChanged, this, [this] { rebuildProps(); m_canvas->update(); });
            m_c2 = connect(d, &Document::frameChanged, m_canvas, qOverload<>(&QWidget::update));
        }
        refresh();
    });
    rebuildProps();
}

void GraphEditorPanel::rebuildProps() {
    const QString keep = m_canvas->property();
    m_props->blockSignals(true);
    m_props->clear();
    if (const Layer* l = m_s->activeLayer()) {
        for (const PropertyDef& d : layerPropertyDefs()) {
            auto it = l->tracks.constFind(d.id);
            if (it != l->tracks.constEnd() && it->animated()) m_props->addItem(d.label, d.id);
        }
    }
    int ix = m_props->findData(keep);
    if (ix < 0 && m_props->count() > 0) ix = 0;
    m_props->setCurrentIndex(ix);
    m_props->blockSignals(false);
    const QString now = ix >= 0 ? m_props->itemData(ix).toString() : QString();
    if (now != keep) m_canvas->setProperty(now);
    m_canvas->update();
}

}  // namespace mynd
