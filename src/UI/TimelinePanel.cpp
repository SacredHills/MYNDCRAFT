#include "UI/TimelinePanel.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace mynd {

namespace {
QColor markerColor(PoseMarker m) {
    switch (m) {
    case PoseMarker::Key: return QColor(0xc0, 0x39, 0x2b);
    case PoseMarker::Breakdown: return QColor(0xd6, 0x89, 0x10);
    case PoseMarker::Inbetween: return QColor(0x27, 0xae, 0x60);
    case PoseMarker::None: break;
    }
    return QColor(0x4a, 0x6a, 0x8a);
}
}  // namespace

// ============================================================ TimelineView
TimelineView::TimelineView(EditorSession* s, AnimationActions* a, QWidget* parent)
    : QAbstractScrollArea(parent), m_s(s), m_a(a) {
    setMouseTracking(false);
    viewport()->setAutoFillBackground(false);
    horizontalScrollBar()->setSingleStep(16);
    auto refresh = [this] { rebuildRows(); viewport()->update(); };
    connect(s, &EditorSession::documentChanged, this, [this, refresh] {
        for (auto* c : {&m_c1, &m_c2, &m_c3, &m_c4, &m_c5, &m_c6, &m_c7}) disconnect(*c);
        if (Document* d = m_s->doc()) {
            m_c1 = connect(d, &Document::layerTreeChanged, this, refresh);
            m_c2 = connect(d, &Document::layerPropsChanged, this, [refresh] { refresh(); });
            m_c3 = connect(d, &Document::animationChanged, this, [refresh] { refresh(); });
            m_c4 = connect(d, &Document::timingChanged, this, refresh);
            m_c6 = connect(d, &Document::tracksChanged, this, refresh);
            m_c7 = connect(d, &Document::scene3DChanged, this, [refresh] { refresh(); });
            m_c5 = connect(d, &Document::frameChanged, this, [this](int f) { ensureFrameVisible(f); viewport()->update(); });
        }
        refresh();
    });
    connect(s, &EditorSession::activeLayerChanged, this, [this] { viewport()->update(); });
    connect(s, &EditorSession::frameSelectionChanged, this, [this] { viewport()->update(); });
    connect(s, &EditorSession::keySelectionChanged, this, [this] { viewport()->update(); });
    setFocusPolicy(Qt::StrongFocus);
    rebuildRows();
}

void TimelineView::setCellWidth(int w) {
    m_cellW = qBound(4, w, 64);
    updateScrollBars();
    viewport()->update();
}

void TimelineView::rebuildRows() {
    m_rows.clear();
    Document* d = m_s->doc();
    if (d) {
        std::function<void(Layer*, int)> walk = [&](Layer* g, int depth) {
            for (int i = int(g->children.size()) - 1; i >= 0; --i) {     // top of the stack first
                Layer* c = g->children[i].get();
                m_rows.append({c, depth, QString()});
                if (m_expanded.contains(c->id)) {
                    for (const PropertyDef& pd : layerPropertyDefs()) m_rows.append({c, depth + 1, pd.id});
                    for (const QString& id : c->animated3DProperties()) m_rows.append({c, depth + 1, id});
                }
                if (c->isGroup()) walk(c, depth + 1);
            }
        };
        walk(d->root(), 0);
    }
    updateScrollBars();
}

void TimelineView::updateScrollBars() {
    const int frames = m_s->doc() ? m_s->doc()->durationFrames() : 0;
    const int contentW = frames * m_cellW + 40;
    const int viewW = viewport()->width() - kNameW;
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewW));
    horizontalScrollBar()->setPageStep(std::max(1, viewW));
    const int contentH = m_rows.size() * kRowH;
    const int viewH = viewport()->height() - kRulerH;
    verticalScrollBar()->setRange(0, std::max(0, contentH - viewH));
    verticalScrollBar()->setPageStep(std::max(1, viewH));
    verticalScrollBar()->setSingleStep(kRowH);
}

void TimelineView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
}

int TimelineView::frameAtX(int x) const {
    return int(std::floor(double(x - kNameW + horizontalScrollBar()->value()) / m_cellW));
}
int TimelineView::rowAtY(int y) const {
    if (y < kRulerH) return -1;
    const int r = (y - kRulerH + verticalScrollBar()->value()) / kRowH;
    return r >= 0 && r < m_rows.size() ? r : -1;
}
int TimelineView::rowOfLayer(const QUuid& id) const {
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].layer->id == id && m_rows[i].prop.isEmpty()) return i;
    return -1;
}
int TimelineView::rowOfProperty(const QUuid& id, const QString& prop) const {
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].layer->id == id && m_rows[i].prop == prop) return i;
    return -1;
}
void TimelineView::setExpanded(const QUuid& id, bool on) {
    if (on == m_expanded.contains(id)) return;
    if (on) m_expanded.insert(id); else m_expanded.remove(id);
    rebuildRows();
    viewport()->update();
}
QPoint TimelineView::twirlPoint(int row) const {
    return {6 + (row >= 0 && row < m_rows.size() ? m_rows[row].depth * 12 : 0) + 4, kRulerH + row * kRowH - verticalScrollBar()->value() + kRowH / 2};
}
bool TimelineView::keyAt(const QPoint& pt, KeyRef* out) const {
    const int r = rowAtY(pt.y());
    if (r < 0 || m_rows[r].prop.isEmpty() || pt.x() < kNameW) return false;
    const Layer* l = m_rows[r].layer;
    auto it = l->tracks.constFind(m_rows[r].prop);
    if (it == l->tracks.constEnd()) return false;
    const int half = keyHalf() + 1;
    for (const Keyframe& k : it->keys) {
        const int cx = xOfFrame(k.frame) + m_cellW / 2;
        if (std::abs(pt.x() - cx) <= half) { *out = {l->id, m_rows[r].prop, k.frame}; return true; }
    }
    return false;
}
QPoint TimelineView::cellCenter(int frame, int row) const {
    return {xOfFrame(frame) + m_cellW / 2, kRulerH + row * kRowH - verticalScrollBar()->value() + kRowH / 2};
}
QPoint TimelineView::rulerPoint(int frame) const { return {xOfFrame(frame) + m_cellW / 2, kRulerH / 2}; }
QRect TimelineView::exposureRect(int row, int frame) const {
    if (row < 0 || row >= m_rows.size() || m_rows[row].layer->type != LayerType::Animation) return {};
    const Exposure* e = m_rows[row].layer->anim.at(frame);
    if (!e) return {};
    return QRect(xOfFrame(e->start), kRulerH + row * kRowH - verticalScrollBar()->value() + 2, e->length * m_cellW, kRowH - 4);
}

void TimelineView::ensureFrameVisible(int frame) {
    QScrollBar* sb = horizontalScrollBar();
    const int left = frame * m_cellW, right = left + m_cellW;
    const int viewW = viewport()->width() - kNameW;
    if (left < sb->value()) sb->setValue(left - 8);
    else if (right > sb->value() + viewW) sb->setValue(right - viewW + 8);
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), palette().color(QPalette::Base));
    Document* d = m_s->doc();
    if (!d) return;
    const int frames = d->durationFrames(), cur = d->currentFrame();
    const int hs = horizontalScrollBar()->value(), vs = verticalScrollBar()->value();
    const int firstF = std::max(0, frameAtX(kNameW)), lastF = std::min(frames - 1, frameAtX(viewport()->width()));
    const QColor grid(255, 255, 255, 22), strong(255, 255, 255, 60);
    const int fpsN = std::max(1, int(std::lround(d->comp().fps.fps())));

    // ---- rows (clipped to the area right of the name column) ----
    p.save();
    p.setClipRect(QRect(kNameW, kRulerH, viewport()->width() - kNameW, viewport()->height() - kRulerH));
    for (int r = 0; r < m_rows.size(); ++r) {
        const int y = kRulerH + r * kRowH - vs;
        if (y + kRowH < kRulerH || y > viewport()->height()) continue;
        const Layer* l = m_rows[r].layer;
        const bool active = l->id == m_s->activeLayerId();
        p.fillRect(QRect(0, y, viewport()->width(), kRowH), active ? QColor(60, 90, 130, 90) : (r % 2 ? QColor(255, 255, 255, 8) : QColor(0, 0, 0, 0)));
        // frame grid
        for (int f = firstF; f <= lastF + 1; ++f) {
            const int x = xOfFrame(f);
            p.setPen((f % fpsN) == 0 ? strong : grid);
            p.drawLine(x, y, x, y + kRowH);
        }
        if (!m_rows[r].prop.isEmpty()) {
            // Property row: one marker per keyframe (diamond = linear, circle = eased, square = hold).
            auto it = l->tracks.constFind(m_rows[r].prop);
            if (it == l->tracks.constEnd()) continue;
            const int half = keyHalf();
            const int cy = y + kRowH / 2;
            for (int i = 0; i + 1 < it->keys.size(); ++i) {      // faint span between neighbouring keys
                const Keyframe &a = it->keys[i], &b = it->keys[i + 1];
                if (b.frame < firstF || a.frame > lastF) continue;
                p.fillRect(QRect(xOfFrame(a.frame) + m_cellW / 2, cy - 1, (b.frame - a.frame) * m_cellW, 2), QColor(255, 140, 0, 70));
            }
            for (const Keyframe& k : it->keys) {
                if (k.frame < firstF - 1 || k.frame > lastF + 1) continue;
                const QPoint c(xOfFrame(k.frame) + m_cellW / 2, cy);
                const bool sel = m_s->isKeySelected(l->id, it.key(), k.frame);
                p.setPen(QPen(sel ? QColor(255, 255, 255) : QColor(40, 40, 40), 1.5));
                p.setBrush(sel ? QColor(255, 200, 60) : QColor(255, 140, 0));
                if (k.out == Interp::Hold) p.drawRect(QRect(c.x() - half + 1, cy - half + 1, 2 * half - 2, 2 * half - 2));
                else if (k.in == Interp::Bezier || k.out == Interp::Bezier) p.drawEllipse(c, half, half);
                else p.drawPolygon(QPolygon({{c.x(), cy - half - 1}, {c.x() + half + 1, cy}, {c.x(), cy + half + 1}, {c.x() - half - 1, cy}}));
            }
            if (m_drag == KeyMove && m_keyDelta != 0) {           // ghosts of the keys being dragged
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::DashLine));
                for (const KeyRef& kr : m_s->selectedKeys())
                    if (kr.layer == l->id && kr.prop == it.key())
                        p.drawEllipse(QPoint(xOfFrame(std::max(0, kr.frame + m_keyDelta)) + m_cellW / 2, cy), half, half);
            }
            continue;
        }
        if (l->type == LayerType::Animation) {
            for (const Exposure& e : l->anim.exposures) {
                if (e.end() <= firstF || e.start > lastF) continue;
                const QRect rc(xOfFrame(e.start), y + 2, e.length * m_cellW, kRowH - 4);
                if (e.drawing.isNull()) {                        // explicit blank exposure: hatched
                    p.fillRect(rc, QColor(90, 90, 90, 120));
                    p.setPen(QPen(QColor(200, 200, 200, 120), 1));
                    p.drawRect(rc.adjusted(0, 0, -1, -1));
                    p.drawLine(rc.topLeft(), rc.bottomRight());
                    continue;
                }
                p.fillRect(rc, markerColor(e.marker));
                p.setPen(QPen(QColor(0, 0, 0, 110), 1));
                p.drawRect(rc.adjusted(0, 0, -1, -1));
                if (const DrawingMeta* dm = l->anim.drawing(e.drawing)) {
                    p.setPen(Qt::white);
                    p.drawText(rc.adjusted(3, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, dm->name);
                }
                // hold ticks: a faint line in every continuation cell
                p.setPen(QColor(255, 255, 255, 70));
                for (int f = e.start + 1; f < e.end(); ++f) {
                    if (f < firstF || f > lastF) continue;
                    const int x = xOfFrame(f) + m_cellW / 2;
                    p.drawLine(x, y + kRowH / 2 - 3, x, y + kRowH / 2 + 3);
                }
            }
            // preview while resizing an exposure edge
            if (m_drag == Resize && l->id == m_resizeLayer) {
                p.setPen(QPen(QColor(255, 255, 0), 2));
                const int x = xOfFrame(m_resizeNewEnd);
                p.drawLine(x, y + 1, x, y + kRowH - 1);
            }
        } else if (!l->isGroup()) {
            p.fillRect(QRect(xOfFrame(0), y + 8, frames * m_cellW, kRowH - 16), l->props.visible ? QColor(110, 110, 110, 150) : QColor(70, 70, 70, 80));
        }
        // Summary of the layer's keyframes along the bottom of its row.
        if (!l->tracks.isEmpty()) {
            QSet<int> keyFrames;
            for (auto it = l->tracks.begin(); it != l->tracks.end(); ++it) for (const Keyframe& k : it->keys) keyFrames.insert(k.frame);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 140, 0, 200));
            for (int f : keyFrames) {
                if (f < firstF - 1 || f > lastF + 1) continue;
                const QPoint c(xOfFrame(f) + m_cellW / 2, y + kRowH - 5);
                p.drawPolygon(QPolygon({{c.x(), c.y() - 4}, {c.x() + 4, c.y()}, {c.x(), c.y() + 4}, {c.x() - 4, c.y()}}));
            }
        }
    }
    p.restore();

    // ---- selected frames ----
    if (m_s->hasFrameSelection()) {
        const int sr = rowOfLayer(m_s->frameSelectionLayer());
        int a, b;
        m_s->selectedFrameRange(&a, &b);
        if (sr >= 0 && m_s->frameSelectionLayer() == m_s->activeLayerId()) {
            const QRect sel(xOfFrame(a), kRulerH + sr * kRowH - vs, (b - a + 1) * m_cellW, kRowH);
            p.save();
            p.setClipRect(QRect(kNameW, kRulerH, viewport()->width() - kNameW, viewport()->height() - kRulerH));
            p.fillRect(sel, QColor(255, 255, 255, 60));
            p.setPen(QPen(QColor(255, 230, 90), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel.adjusted(1, 1, -1, -1));
            p.restore();
        }
    }

    // ---- name column ----
    p.fillRect(QRect(0, kRulerH, kNameW, viewport()->height() - kRulerH), palette().color(QPalette::Window));
    p.save();
    p.setClipRect(QRect(0, kRulerH, kNameW, viewport()->height() - kRulerH));
    for (int r = 0; r < m_rows.size(); ++r) {
        const int y = kRulerH + r * kRowH - vs;
        if (y + kRowH < kRulerH || y > viewport()->height()) continue;
        const Layer* l = m_rows[r].layer;
        const bool active = l->id == m_s->activeLayerId();
        if (active) p.fillRect(QRect(0, y, kNameW, kRowH), m_rows[r].prop.isEmpty() ? QColor(60, 90, 130) : QColor(60, 90, 130, 60));
        p.setPen(l->props.visible ? palette().color(QPalette::Text) : palette().color(QPalette::Disabled, QPalette::Text));
        if (!m_rows[r].prop.isEmpty()) {
            const PropertyDef* pd = findPropertyDef(m_rows[r].prop);
            const bool animated = l->tracks.contains(m_rows[r].prop) && l->tracks[m_rows[r].prop].animated();
            p.setPen(animated ? QColor(255, 170, 60) : palette().color(QPalette::Text));
            p.drawText(QRect(6 + m_rows[r].depth * 12 + 10, y, kNameW - 20, kRowH), Qt::AlignVCenter | Qt::AlignLeft, l->propertyLabel(m_rows[r].prop));
        } else {
            const QPoint tw = twirlPoint(r);                  // disclosure arrow
            const bool open = m_expanded.contains(l->id);
            QPolygon arrow = open ? QPolygon({{tw.x() - 4, tw.y() - 2}, {tw.x() + 4, tw.y() - 2}, {tw.x(), tw.y() + 3}})
                                  : QPolygon({{tw.x() - 2, tw.y() - 4}, {tw.x() - 2, tw.y() + 4}, {tw.x() + 3, tw.y()}});
            p.save(); p.setBrush(p.pen().color()); p.drawPolygon(arrow); p.restore();
            QString t = l->props.name + (l->props.locked ? "  🔒" : "");
            p.drawText(QRect(6 + m_rows[r].depth * 12 + 12, y, kNameW - 22, kRowH), Qt::AlignVCenter | Qt::AlignLeft, t);
        }
        p.setPen(QColor(255, 255, 255, 25));
        p.drawLine(0, y + kRowH - 1, kNameW, y + kRowH - 1);
    }
    p.restore();

    // ---- ruler ----
    p.fillRect(QRect(0, 0, viewport()->width(), kRulerH), palette().color(QPalette::Window).darker(115));
    p.save();
    p.setClipRect(QRect(kNameW, 0, viewport()->width() - kNameW, kRulerH));
    if (d->comp().workEnabled)
        p.fillRect(QRect(xOfFrame(d->comp().workIn), 0, (d->comp().workOut - d->comp().workIn + 1) * m_cellW, kRulerH), QColor(80, 140, 220, 90));
    const int labelEvery = m_cellW >= 28 ? 1 : m_cellW >= 14 ? 2 : m_cellW >= 8 ? 5 : 10;
    for (int f = firstF; f <= lastF; ++f) {
        const int x = xOfFrame(f);
        const bool sec = (f % fpsN) == 0;
        p.setPen(sec ? QColor(220, 220, 220) : QColor(150, 150, 150));
        p.drawLine(x, kRulerH - (sec ? 10 : 5), x, kRulerH - 1);
        if ((f + 1) % labelEvery == 0 || f == 0) {
            p.setPen(palette().color(QPalette::Text));
            p.drawText(QRect(x + 2, 0, std::max(m_cellW * labelEvery, 24), kRulerH - 8), Qt::AlignTop | Qt::AlignLeft, QString::number(f + 1));
        }
    }
    p.restore();
    p.setPen(QColor(255, 255, 255, 70));
    p.drawLine(0, kRulerH - 1, viewport()->width(), kRulerH - 1);

    if (m_drag == Marquee) {
        p.setPen(QPen(QColor(255, 255, 255, 220), 1, Qt::DashLine));
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawRect(QRect(m_marqueeStart, m_marqueeNow).normalized());
    }

    // ---- end of composition ----
    p.setPen(QColor(255, 255, 255, 90));
    p.drawLine(xOfFrame(frames), kRulerH, xOfFrame(frames), viewport()->height());

    // ---- playhead ----
    if (cur >= firstF - 1 && cur <= lastF + 1) {
        const int x = xOfFrame(cur);
        p.fillRect(QRect(x, kRulerH, m_cellW, viewport()->height() - kRulerH), QColor(255, 60, 60, 38));
        p.setPen(QPen(QColor(255, 70, 70), 2));
        p.drawLine(x + m_cellW / 2, 0, x + m_cellW / 2, viewport()->height());
        p.fillRect(QRect(x, 0, m_cellW, kRulerH - 10), QColor(255, 70, 70));
    }
    Q_UNUSED(hs);
}

bool TimelineView::hitExposureEdge(const QPoint& pt, int* row, int* exposureIndex) const {
    const int r = rowAtY(pt.y());
    if (r < 0 || m_rows[r].layer->type != LayerType::Animation) return false;
    const Layer* l = m_rows[r].layer;
    for (int i = 0; i < l->anim.exposures.size(); ++i) {
        const Exposure& e = l->anim.exposures[i];
        if (std::abs(pt.x() - xOfFrame(e.end())) <= 4) { *row = r; *exposureIndex = i; return true; }
    }
    return false;
}

void TimelineView::scrubTo(int x) {
    if (Document* d = m_s->doc()) d->setCurrentFrame(frameAtX(x));
}

void TimelineView::mousePressEvent(QMouseEvent* e) {
    Document* d = m_s->doc();
    if (!d || e->button() != Qt::LeftButton) return;
    const QPoint pt = e->position().toPoint();
    if (pt.y() < kRulerH) {
        if (pt.x() >= kNameW) { m_s->playback()->pause(); m_drag = Scrub; scrubTo(pt.x()); }
        return;
    }
    const int r = rowAtY(pt.y());
    setFocus();
    if (r >= 0) m_s->setActiveLayer(m_rows[r].layer->id);
    if (r >= 0 && pt.x() < kNameW && m_rows[r].prop.isEmpty() && std::abs(pt.x() - twirlPoint(r).x()) <= 8) {
        setExpanded(m_rows[r].layer->id, !m_expanded.contains(m_rows[r].layer->id));    // twirl
        return;
    }
    if (r >= 0 && pt.x() >= kNameW && !m_rows[r].prop.isEmpty()) {
        m_s->playback()->pause();
        KeyRef kr;
        if (keyAt(pt, &kr)) {
            const bool multi = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            QVector<KeyRef> sel = m_s->selectedKeys();
            if (multi) { if (sel.contains(kr)) sel.removeAll(kr); else sel.push_back(kr); }
            else if (!sel.contains(kr)) sel = {kr};
            m_s->setKeySelection(sel);
            d->setCurrentFrame(kr.frame);
            if (m_s->isKeySelected(kr.layer, kr.prop, kr.frame)) { m_drag = KeyMove; m_keyGrabFrame = kr.frame; m_keyDelta = 0; }
            return;
        }
        // empty cell: drag a marquee; a plain click also moves the playhead
        m_drag = Marquee; m_marqueeStart = m_marqueeNow = pt;
        m_marqueeAdd = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (!m_marqueeAdd) m_s->clearKeySelection();
        d->setCurrentFrame(std::clamp(frameAtX(pt.x()), 0, d->durationFrames() - 1));
        return;
    }
    int er, ei;
    if (pt.x() >= kNameW && hitExposureEdge(pt, &er, &ei)) {
        const Layer* l = m_rows[er].layer;
        if (l->props.locked) { emit m_s->message(tr("The layer is locked.")); return; }
        const Exposure& ex = l->anim.exposures[ei];
        m_drag = Resize; m_resizeLayer = l->id; m_resizeStart = ex.start; m_resizeEnd = ex.end(); m_resizeNewEnd = ex.end();
        m_resizeFrame = ex.start;
        return;
    }
    if (pt.x() >= kNameW) {
        m_s->playback()->pause();
        const int f = std::clamp(frameAtX(pt.x()), 0, d->durationFrames() - 1);
        const Layer* l = r >= 0 ? m_rows[r].layer : nullptr;
        if (l && l->type == LayerType::Animation) {
            // Click selects a frame; Shift+click extends from the previous anchor; dragging selects a range.
            if ((e->modifiers() & Qt::ShiftModifier) && m_s->hasFrameSelection() && m_s->frameSelectionLayer() == l->id) {
                int a, b;
                m_s->selectedFrameRange(&a, &b);
                m_anchor = d->currentFrame() == b ? a : b;       // keep the end that is not under the playhead
            } else if ((e->modifiers() & Qt::ShiftModifier) && d->currentFrame() != f && l->id == m_s->activeLayerId()) {
                m_anchor = d->currentFrame();                    // Shift+click from the playhead with no selection
            } else {
                m_anchor = f;
            }
            m_dragLayer = l->id;
            m_drag = SelectRange;
            m_s->setFrameSelection(l->id, m_anchor, f);
        } else {
            m_s->clearFrameSelection();
            d->setCurrentFrame(f);
        }
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    if (m_drag == Scrub) { scrubTo(pt.x()); return; }
    if (m_drag == KeyMove) {
        m_keyDelta = std::max(-m_keyGrabFrame, frameAtX(pt.x()) - m_keyGrabFrame);   // (clamped again when applied)
        viewport()->update();
        return;
    }
    if (m_drag == Marquee) {
        m_marqueeNow = pt;
        viewport()->update();
        return;
    }
    if (m_drag == SelectRange) {
        if (Document* d = m_s->doc())
            m_s->setFrameSelection(m_dragLayer, m_anchor, std::clamp(frameAtX(pt.x()), 0, d->durationFrames() - 1));
        return;
    }
    if (m_drag == Resize) {
        const int f = int(std::lround(double(pt.x() - kNameW + horizontalScrollBar()->value()) / m_cellW));
        m_resizeNewEnd = std::max(m_resizeStart + 1, f);
        viewport()->update();
        return;
    }
    int r, i;
    viewport()->setCursor(pt.x() >= kNameW && hitExposureEdge(pt, &r, &i) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag == KeyMove) {
        const int delta = m_keyDelta;
        m_drag = None; m_keyDelta = 0;
        if (delta != 0) m_s->moveSelectedKeys(delta);
        viewport()->update();
        return;
    }
    if (m_drag == Marquee) {
        const QRect box = QRect(m_marqueeStart, m_marqueeNow).normalized();
        m_drag = None;
        if (box.width() > 3 || box.height() > 3) {
            QVector<KeyRef> sel = m_marqueeAdd ? m_s->selectedKeys() : QVector<KeyRef>();
            for (int r = 0; r < m_rows.size(); ++r) {
                if (m_rows[r].prop.isEmpty()) continue;
                const int cy = kRulerH + r * kRowH - verticalScrollBar()->value() + kRowH / 2;
                if (cy < box.top() || cy > box.bottom()) continue;
                auto it = m_rows[r].layer->tracks.constFind(m_rows[r].prop);
                if (it == m_rows[r].layer->tracks.constEnd()) continue;
                for (const Keyframe& k : it->keys) {
                    const int cx = xOfFrame(k.frame) + m_cellW / 2;
                    const KeyRef kr{m_rows[r].layer->id, m_rows[r].prop, k.frame};
                    if (cx >= box.left() && cx <= box.right() && !sel.contains(kr)) sel.push_back(kr);
                }
            }
            m_s->setKeySelection(sel);
        }
        viewport()->update();
        return;
    }
    if (m_drag == Resize) {
        const int delta = m_resizeNewEnd - m_resizeEnd;
        m_drag = None;
        if (delta != 0 && m_s->activeLayerId() == m_resizeLayer) {
            const int f = m_resizeFrame;
            m_s->editActiveAnimation(delta > 0 ? tr("Extend Exposure") : tr("Shorten Exposure"), [&](AnimationState& s) {
                return delta > 0 ? s.extendExposure(f, delta) : s.shortenExposure(f, -delta);
            });
        }
        viewport()->update();
        return;
    }
    m_drag = None;
}

void TimelineView::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const int step = e->angleDelta().y() > 0 ? 2 : -2;
        setCellWidth(m_cellW + step);
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y() / 2);
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void TimelineView::contextMenuEvent(QContextMenuEvent* e) {
    const QPoint pt = e->pos();
    const int r = rowAtY(pt.y());
    if (r >= 0) m_s->setActiveLayer(m_rows[r].layer->id);
    if (r >= 0 && pt.x() >= kNameW && !m_rows[r].prop.isEmpty()) {
        KeyRef kr;
        if (keyAt(pt, &kr)) {
            if (!m_s->selectedKeys().contains(kr)) m_s->setKeySelection({kr});
            QMenu menu(this);
            m_a->fillKeyframeMenu(&menu);
            menu.exec(e->globalPos());
            return;
        }
        // empty cell of a property row: add a key there / paste
        const int f = std::clamp(frameAtX(pt.x()), 0, m_s->doc() ? m_s->doc()->durationFrames() - 1 : 0);
        if (m_s->doc()) m_s->doc()->setCurrentFrame(f);
        QMenu menu(this);
        const Layer* l = m_rows[r].layer; const QString prop = m_rows[r].prop;
        connect(menu.addAction(tr("Add Keyframe Here")), &QAction::triggered, &menu, [this, l, prop] { m_s->toggleKeyHere(l->id, prop); });
        QAction* paste = menu.addAction(tr("Paste Keyframes Here"));
        paste->setEnabled(!m_s->keyClip().isEmpty());
        connect(paste, &QAction::triggered, &menu, [this] { m_s->pasteKeys(); });
        menu.exec(e->globalPos());
        return;
    }
    if (pt.x() >= kNameW && r >= 0) if (Document* d = m_s->doc()) {
        const int f = std::clamp(frameAtX(pt.x()), 0, d->durationFrames() - 1);
        int a, b;
        m_s->selectedFrameRange(&a, &b);
        const bool insideSelection = m_s->hasFrameSelection() && f >= a && f <= b;
        if (!insideSelection) {                         // right-click outside the selection selects that frame
            if (m_rows[r].layer->type == LayerType::Animation) m_s->setFrameSelection(m_rows[r].layer->id, f, f);
            else d->setCurrentFrame(f);
        }
    }
    QMenu menu(this);
    m_a->fillContextMenu(&menu);
    menu.exec(e->globalPos());
}

void TimelineView::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && !m_s->selectedKeys().isEmpty()) {
        m_s->deleteSelectedKeys();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape && m_drag == KeyMove) { m_drag = None; m_keyDelta = 0; viewport()->update(); return; }
    QAbstractScrollArea::keyPressEvent(e);
}

// ============================================================ TimelinePanel
TimelinePanel::TimelinePanel(EditorSession* s, AnimationActions* a, QWidget* parent) : QWidget(parent), m_s(s), m_a(a) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(3);
    auto* bar = new QHBoxLayout;
    // Compact glyph buttons that mirror an action (a default action would force the long action text).
    auto btn = [&](QAction* act, const QString& glyph) {
        auto* b = new QToolButton;
        b->setText(glyph);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setToolTip(act->text() + (act->shortcut().isEmpty() ? QString() : "  (" + act->shortcut().toString(QKeySequence::NativeText) + ")"));
        b->setEnabled(act->isEnabled());
        b->setCheckable(act->isCheckable());
        b->setChecked(act->isChecked());
        connect(b, &QToolButton::clicked, act, [act] { act->trigger(); });
        connect(act, &QAction::changed, b, [b, act] { b->setEnabled(act->isEnabled()); });
        if (act->isCheckable()) connect(act, &QAction::toggled, b, &QToolButton::setChecked);
        bar->addWidget(b);
        return b;
    };
    btn(a->firstFrame, "|◀"); btn(a->prevFrame, "◀");
    auto* play = btn(a->playPause, "▷");
    connect(s->playback(), &Playback::playingChanged, this, [play](bool p) { play->setText(p ? "❚❚" : "▷"); });
    btn(a->stopPlayback, "■"); btn(a->nextFrame, "▶"); btn(a->lastFrame, "▶|");
    btn(a->loopPlayback, tr("Loop"));
    btn(a->onionToggle, tr("Onion"));
    btn(a->autoKey, tr("Auto-Key"));
    bar->addSpacing(8);
    bar->addWidget(new QLabel(tr("Frame")));
    m_frame = new QSpinBox; m_frame->setRange(1, 1); m_frame->setKeyboardTracking(false);
    bar->addWidget(m_frame);
    m_time = new QLabel("00:00:00:00"); m_time->setMinimumWidth(86);
    bar->addWidget(m_time);
    bar->addSpacing(8);
    bar->addWidget(new QLabel(tr("Length")));
    m_duration = new QSpinBox; m_duration->setRange(1, kMaxDurationFrames); m_duration->setSuffix(tr(" fr")); m_duration->setKeyboardTracking(false);
    bar->addWidget(m_duration);
    m_fps = new QComboBox;
    const struct { const char* label; int n, d; } rates[] = {
        {"8 fps", 8, 1}, {"12 fps", 12, 1}, {"15 fps", 15, 1}, {"23.976 fps", 24000, 1001}, {"24 fps", 24, 1}, {"25 fps", 25, 1},
        {"29.97 fps", 30000, 1001}, {"30 fps", 30, 1}, {"48 fps", 48, 1}, {"50 fps", 50, 1}, {"59.94 fps", 60000, 1001}, {"60 fps", 60, 1}};
    for (const auto& r : rates) m_fps->addItem(r.label, QVariant::fromValue(QPoint(r.n, r.d)));
    bar->addWidget(m_fps);
    m_speed = new QComboBox;
    for (int pct : {25, 50, 100, 150, 200}) m_speed->addItem(QString("%1%").arg(pct), pct);
    m_speed->setCurrentIndex(2);
    m_speed->setToolTip(tr("Playback speed"));
    bar->addWidget(m_speed);
    bar->addStretch();
    bar->addWidget(new QLabel(tr("Zoom")));
    m_zoom = new QSlider(Qt::Horizontal); m_zoom->setRange(4, 64); m_zoom->setValue(16); m_zoom->setMaximumWidth(110);
    bar->addWidget(m_zoom);
    lay->addLayout(bar);
    m_view = new TimelineView(s, a);
    lay->addWidget(m_view, 1);

    connect(m_frame, &QSpinBox::valueChanged, this, [this](int v) { if (!m_busy && m_s->doc()) m_s->doc()->setCurrentFrame(v - 1); });
    connect(m_duration, &QSpinBox::valueChanged, this, [this](int v) { if (!m_busy && m_s->doc()) m_s->doc()->setDuration(v); });
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        const QPoint r = m_fps->itemData(i).toPoint();
        if (m_s->doc()) m_s->doc()->setFrameRate({r.x(), r.y()});
    });
    connect(m_speed, &QComboBox::activated, this, [this](int i) { m_s->playback()->setSpeedPercent(m_speed->itemData(i).toInt()); });
    connect(m_zoom, &QSlider::valueChanged, m_view, &TimelineView::setCellWidth);
    connect(s, &EditorSession::documentChanged, this, [this] {
        if (Document* d = m_s->doc()) {
            connect(d, &Document::frameChanged, this, &TimelinePanel::syncControls);
            connect(d, &Document::timingChanged, this, &TimelinePanel::syncControls);
        }
        syncControls();
    });
    syncControls();
}

QString TimelinePanel::timecode(int frame) const {
    const Document* d = m_s->doc();
    if (!d) return "00:00:00:00";
    const int nominal = std::max(1, int(std::lround(d->comp().fps.fps())));
    const int sec = frame / nominal, ff = frame % nominal;
    return QString("%1:%2:%3:%4").arg(sec / 3600, 2, 10, QChar('0')).arg(sec / 60 % 60, 2, 10, QChar('0'))
        .arg(sec % 60, 2, 10, QChar('0')).arg(ff, 2, 10, QChar('0'));
}

void TimelinePanel::syncControls() {
    m_busy = true;
    const Document* d = m_s->doc();
    setEnabled(d != nullptr);
    if (d) {
        m_frame->setRange(1, d->durationFrames());
        m_frame->setValue(d->currentFrame() + 1);
        m_duration->setValue(d->durationFrames());
        m_time->setText(timecode(d->currentFrame()));
        const FrameRate f = d->comp().fps;
        int idx = -1;
        for (int i = 0; i < m_fps->count(); ++i) {
            const QPoint r = m_fps->itemData(i).toPoint();
            if (r.x() == f.num && r.y() == f.den) idx = i;
        }
        if (idx < 0) {   // a rate that is not in the presets (from an opened file): show it, keep it selectable
            m_fps->addItem(QString("%1 fps").arg(f.fps(), 0, 'g', 6), QVariant::fromValue(QPoint(f.num, f.den)));
            idx = m_fps->count() - 1;
        }
        m_fps->setCurrentIndex(idx);
    }
    m_busy = false;
}

}  // namespace mynd
