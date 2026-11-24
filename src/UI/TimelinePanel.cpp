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
        for (auto* c : {&m_c1, &m_c2, &m_c3, &m_c4, &m_c5}) disconnect(*c);
        if (Document* d = m_s->doc()) {
            m_c1 = connect(d, &Document::layerTreeChanged, this, refresh);
            m_c2 = connect(d, &Document::layerPropsChanged, this, [refresh] { refresh(); });
            m_c3 = connect(d, &Document::animationChanged, this, [refresh] { refresh(); });
            m_c4 = connect(d, &Document::timingChanged, this, refresh);
            m_c5 = connect(d, &Document::frameChanged, this, [this](int f) { ensureFrameVisible(f); viewport()->update(); });
        }
        refresh();
    });
    connect(s, &EditorSession::activeLayerChanged, this, [this] { viewport()->update(); });
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
                m_rows.append({c, depth});
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
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].layer->id == id) return i;
    return -1;
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
    }
    p.restore();

    // ---- name column ----
    p.fillRect(QRect(0, kRulerH, kNameW, viewport()->height() - kRulerH), palette().color(QPalette::Window));
    p.save();
    p.setClipRect(QRect(0, kRulerH, kNameW, viewport()->height() - kRulerH));
    for (int r = 0; r < m_rows.size(); ++r) {
        const int y = kRulerH + r * kRowH - vs;
        if (y + kRowH < kRulerH || y > viewport()->height()) continue;
        const Layer* l = m_rows[r].layer;
        const bool active = l->id == m_s->activeLayerId();
        if (active) p.fillRect(QRect(0, y, kNameW, kRowH), QColor(60, 90, 130));
        p.setPen(l->props.visible ? palette().color(QPalette::Text) : palette().color(QPalette::Disabled, QPalette::Text));
        QString t = l->props.name + (l->props.locked ? "  🔒" : "");
        p.drawText(QRect(6 + m_rows[r].depth * 12, y, kNameW - 10, kRowH), Qt::AlignVCenter | Qt::AlignLeft, t);
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
    if (r >= 0) m_s->setActiveLayer(m_rows[r].layer->id);
    int er, ei;
    if (pt.x() >= kNameW && hitExposureEdge(pt, &er, &ei)) {
        const Layer* l = m_rows[er].layer;
        if (l->props.locked) { emit m_s->message(tr("The layer is locked.")); return; }
        const Exposure& ex = l->anim.exposures[ei];
        m_drag = Resize; m_resizeLayer = l->id; m_resizeStart = ex.start; m_resizeEnd = ex.end(); m_resizeNewEnd = ex.end();
        m_resizeFrame = ex.start;
        return;
    }
    if (pt.x() >= kNameW) { m_s->playback()->pause(); d->setCurrentFrame(frameAtX(pt.x())); }
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    if (m_drag == Scrub) { scrubTo(pt.x()); return; }
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
    if (pt.x() >= kNameW && r >= 0) if (Document* d = m_s->doc()) d->setCurrentFrame(frameAtX(pt.x()));
    QMenu menu(this);
    m_a->fillContextMenu(&menu);
    menu.exec(e->globalPos());
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
