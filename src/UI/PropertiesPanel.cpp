#include "UI/PropertiesPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

namespace mynd {

namespace {

QIcon stopwatchIcon(bool on, const QPalette&) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = on ? QColor(255, 140, 0) : QColor(170, 170, 170);   // (fixed greys: the app theme is dark whatever the style's palette says)
    p.setPen(QPen(c, 2.6));
    p.setBrush(on ? QColor(255, 140, 0, 60) : Qt::NoBrush);
    p.drawEllipse(QPointF(16, 18), 10, 10);
    p.drawLine(QPointF(16, 18), QPointF(16, 11));
    p.drawLine(QPointF(16, 18), QPointF(21, 20));
    p.drawLine(QPointF(13, 5), QPointF(19, 5));
    return QIcon(pm);
}

QIcon diamondIcon(bool filled, bool enabled, const QPalette&) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = enabled ? QColor(255, 140, 0) : QColor(110, 110, 110);
    p.setPen(QPen(c, 2.4));
    p.setBrush(filled ? c : Qt::NoBrush);
    p.drawPolygon(QPolygonF({{16, 4}, {28, 16}, {16, 28}, {4, 16}}));
    return QIcon(pm);
}

}  // namespace

PropertiesPanel::PropertiesPanel(EditorSession* s, AnimationActions* a, QWidget* parent) : QWidget(parent), m_s(s), m_a(a) {
    // Everything lives in a scroll area so a narrow dock scrolls instead of overlapping its neighbours.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    setMinimumWidth(180);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(6, 6, 6, 6);
    m_layerName = new QLabel;
    m_layerName->setStyleSheet("font-weight: bold");
    lay->addWidget(m_layerName);
    m_auto = new QCheckBox(tr("Auto-Key"));
    m_auto->setToolTip(tr("Create a keyframe at the current frame whenever you change a property"));
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) { m_s->setAutoKey(on); });
    connect(m_s, &EditorSession::autoKeyChanged, this, [this] { QSignalBlocker b(m_auto); m_auto->setChecked(m_s->autoKey()); });
    lay->addWidget(m_auto);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(6);
    int row = 0;
    for (const PropertyDef& d : layerPropertyDefs()) {
        Row r;
        r.watch = new QToolButton; r.watch->setCheckable(true); r.watch->setAutoRaise(true);
        r.watch->setToolTip(tr("Animate %1 (stopwatch)").arg(d.label));
        r.prev = new QToolButton; r.prev->setText("◂"); r.prev->setAutoRaise(true); r.prev->setToolTip(tr("Previous keyframe"));
        r.key = new QToolButton; r.key->setAutoRaise(true); r.key->setToolTip(tr("Add or remove a keyframe at the current frame"));
        r.next = new QToolButton; r.next->setText("▸"); r.next->setAutoRaise(true); r.next->setToolTip(tr("Next keyframe"));
        r.label = new QLabel(d.label);
        const QString id = d.id;
        connect(r.watch, &QToolButton::clicked, this, [this, id] { m_s->toggleAnimated(m_s->activeLayerId(), id); });
        connect(r.key, &QToolButton::clicked, this, [this, id] { m_s->toggleKeyHere(m_s->activeLayerId(), id); });
        connect(r.prev, &QToolButton::clicked, this, [this, id] { gotoPropertyKey(id, -1); });
        connect(r.next, &QToolButton::clicked, this, [this, id] { gotoPropertyKey(id, +1); });
        grid->addWidget(r.watch, row, 0);
        grid->addWidget(r.label, row, 1);
        const int n = propComponents(d.type);
        auto* vals = new QHBoxLayout;
        for (int c = 0; c < n; ++c) {
            auto* sp = new QDoubleSpinBox;
            sp->setRange(d.minV.c[c], d.maxV.c[c]);
            sp->setDecimals(d.decimals);
            sp->setSingleStep(d.type == PropType::Rotation ? 1.0 : 1.0);
            sp->setKeyboardTracking(false);
            sp->setSuffix(" " + d.unit);
            sp->setMinimumWidth(78);
            if (n > 1) sp->setPrefix(d.componentNames.value(c) + " ");
            sp->setAccelerated(true);
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, id] { if (!m_busy) edited(id); });
            vals->addWidget(sp);
            r.spins << sp;
        }
        grid->addLayout(vals, row, 2);
        auto* nav = new QHBoxLayout;
        nav->setSpacing(0);
        nav->addWidget(r.prev); nav->addWidget(r.key); nav->addWidget(r.next);
        grid->addLayout(nav, row, 3);
        m_rows.insert(id, r);
        ++row;
        if (id == "transform.scale") {
            m_link = new QCheckBox(tr("Keep proportions"));
            m_link->setChecked(true);
            grid->addWidget(m_link, row, 2);
            ++row;
        }
    }
    grid->setColumnStretch(2, 1);
    lay->addLayout(grid);
    auto* hint = new QLabel(tr("Anchor and Position are offsets in pixels from the layer's untransformed place; "
                               "Scale and Opacity are percentages. Use the Transform Layer tool (W) to drag on the canvas."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray");
    lay->addWidget(hint);
    lay->addStretch(1);

    auto sync = [this] { refresh(); };
    connect(s, &EditorSession::activeLayerChanged, this, sync);
    connect(s, &EditorSession::documentChanged, this, [this, sync] {
        if (Document* d = m_s->doc()) {
            connect(d, &Document::frameChanged, this, [this] { refresh(); });
            connect(d, &Document::tracksChanged, this, [this] { refresh(); });
            connect(d, &Document::layerTreeChanged, this, [this] { refresh(); });
            connect(d, &Document::layerPropsChanged, this, [this] { refresh(); });
        }
        refresh();
    });
    refresh();
}

QDoubleSpinBox* PropertiesPanel::field(const QString& prop, int component) const {
    const Row r = m_rows.value(prop);
    return component >= 0 && component < r.spins.size() ? r.spins[component] : nullptr;
}

void PropertiesPanel::refresh() {
    m_busy = true;
    const Layer* l = m_s->activeLayer();
    const bool on = l && !l->props.locked;
    m_layerName->setText(l ? l->props.name : tr("No layer selected"));
    {
        QSignalBlocker b(m_auto);
        m_auto->setChecked(m_s->autoKey());
    }
    const int frame = m_s->doc() ? m_s->doc()->currentFrame() : 0;
    const QPalette pal = palette();
    for (const PropertyDef& d : layerPropertyDefs()) {
        Row& r = m_rows[d.id];
        const auto tit = l ? l->tracks.constFind(d.id) : PropertyTracks::const_iterator();
        const PropertyTrack* tr = l && tit != l->tracks.constEnd() ? &tit.value() : nullptr;
        const bool animated = tr && tr->animated();
        const bool keyHere = animated && tr->indexOfKey(frame) >= 0;
        r.watch->setEnabled(on);
        r.watch->setChecked(animated);
        r.watch->setIcon(stopwatchIcon(animated, pal));
        r.key->setEnabled(on);
        r.key->setIcon(diamondIcon(keyHere, on, pal));
        r.prev->setEnabled(animated); r.next->setEnabled(animated);
        const PropValue v = l ? valueOf(l->tracks, d, frame) : d.def;
        for (int c = 0; c < r.spins.size(); ++c) {
            r.spins[c]->setEnabled(on);
            QSignalBlocker b(r.spins[c]);
            r.spins[c]->setValue(v.c[c]);
        }
    }
    m_busy = false;
}

void PropertiesPanel::edited(const QString& prop) {
    const PropertyDef* d = findPropertyDef(prop);
    if (!d || !m_s->activeLayer()) return;
    Row& r = m_rows[prop];
    PropValue v;
    for (int c = 0; c < r.spins.size(); ++c) v.c[c] = r.spins[c]->value();
    QVector<QPair<QString, PropValue>> vals{{prop, v}};
    if (prop == "transform.scale" && m_link->isChecked() && r.spins.size() == 2) {
        // Proportional scaling: the field that was not edited follows by the same ratio.
        const PropValue before = m_s->propertyValue(m_s->activeLayerId(), prop);
        const bool xChanged = qAbs(v.c[0] - before.c[0]) > 1e-9;
        const double ratio = xChanged ? (before.c[0] != 0 ? v.c[0] / before.c[0] : 1) : (before.c[1] != 0 ? v.c[1] / before.c[1] : 1);
        if (xChanged) v.c[1] = before.c[1] * ratio; else v.c[0] = before.c[0] * ratio;
        vals = {{prop, v}};
    }
    // Typing several values in a row within a second is one undo step.
    if (!m_lastEdit.isValid() || m_lastEdit.elapsed() > 1000) ++m_editCounter;
    m_lastEdit.restart();
    m_s->setProperties(m_s->activeLayerId(), vals, QString("panel-%1-%2").arg(prop).arg(m_editCounter));
}

void PropertiesPanel::gotoPropertyKey(const QString& prop, int dir) {
    const Layer* l = m_s->activeLayer();
    if (!l || !m_s->doc() || !l->tracks.contains(prop)) return;
    const int cur = m_s->doc()->currentFrame();
    int best = -1;
    for (const Keyframe& k : l->tracks[prop].keys) {
        if (dir > 0 && k.frame > cur) { best = k.frame; break; }
        if (dir < 0 && k.frame < cur) best = k.frame;
    }
    if (best >= 0) m_s->doc()->setCurrentFrame(best);
}

}  // namespace mynd
