#include "UI/EffectsPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

#include "Scene/Document.h"
#include "UI/ScrubSpinBox.h"

namespace mynd {

namespace {
QDoubleSpinBox* spin(double lo, double hi, double v, int dec, const QString& suffix = {}) {
    auto* s = new QDoubleSpinBox; s->setRange(lo, hi); s->setDecimals(dec); s->setValue(v); s->setSuffix(suffix);
    s->setKeyboardTracking(false); s->setMinimumWidth(64);
    return s;
}
}  // namespace

EffectsPanel::EffectsPanel(EditorSession* s, bool embedded, QWidget* parent) : QWidget(parent), m_s(s), m_embedded(embedded) {
    setObjectName("effectsPanel");
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    if (embedded) {
        m_body = this;
        m_layout = outer;
    } else {
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        outer->addWidget(scroll);
        m_body = new QWidget; scroll->setWidget(m_body);
        m_layout = new QVBoxLayout(m_body); m_layout->setAlignment(Qt::AlignTop);
    }
    connect(s, &EditorSession::documentChanged, this, [this] { watchDocument(); rebuild(); });
    connect(s, &EditorSession::activeLayerChanged, this, [this] { rebuild(); });
    watchDocument();
    rebuild();
}

// (Re)connects to the current document's signals, once per document.
void EffectsPanel::watchDocument() {
    Document* d = m_s->doc();
    if (d == m_watched) return;
    for (const QMetaObject::Connection& c : m_conns) disconnect(c);
    m_conns.clear();
    m_watched = d;
    if (!d) return;
    m_conns << connect(d, &Document::layerTreeChanged, this, &EffectsPanel::refreshIfChanged)
            << connect(d, &Document::frameChanged, this, [this] { updateValues(); })
            << connect(d, &Document::tracksChanged, this, [this] { updateValues(); });
}

void EffectsPanel::refreshIfChanged() {
    Layer* l = m_s->activeLayer();
    if (!l) { if (!m_shownLayer.isNull()) rebuild(); return; }
    if (l->id != m_shownLayer || !(l->fx == m_shown)) rebuild();       // edits made by this panel's own widgets do not rebuild under the user's cursor
}

void EffectsPanel::edit(const QString& text, const QString& mergeKey, const std::function<void(LayerFx&)>& fn) {
    Layer* l = m_s->activeLayer();
    if (!l || l->props.locked || !m_s->doc()) return;
    LayerFx fx = l->fx;
    fn(fx);
    m_shown = fx; m_shownLayer = l->id;                                 // the widgets already show this state
    // keyframes of effects/masks that no longer exist go with them (one undo step)
    PropertyTracks pruned = l->tracks; bool orphans = false;
    const QVector<QString> valid = fxPropertyIds(fx);
    for (const QString& k : l->tracks.keys()) if (isFxPropId(k) && !valid.contains(k)) { pruned.remove(k); orphans = true; }
    Document* d = m_s->doc();
    if (orphans) {
        d->undoStack()->beginMacro(text);
        d->setLayerFx(l->id, fx, text);
        d->editTracks(l->id, pruned, text);
        d->undoStack()->endMacro();
    } else {
        d->setLayerFx(l->id, fx, text, mergeKey.isEmpty() ? QString() : mergeKey + l->id.toString());
    }
}

QWidget* EffectsPanel::valueRow(const QString& label, const QString& propId, double lo, double hi, int decimals, const QString& suffix) {
    auto* w = new QWidget;
    auto* r = new QHBoxLayout(w); r->setContentsMargins(0, 0, 0, 0); r->setSpacing(3);
    auto* lab = new QLabel(label); lab->setMinimumWidth(40);
    r->addWidget(lab, 1);
    auto* sp = new ScrubSpinBox; sp->setRange(lo, hi); sp->setDecimals(decimals); sp->setSuffix(suffix);
    sp->setSingleStep((hi - lo) >= 150 ? 1.0 : 0.5); sp->setKeyboardTracking(false); sp->setMinimumWidth(64);
    auto* watch = new QToolButton; watch->setCheckable(true); watch->setAutoRaise(true); watch->setText(QString::fromUtf8("⏱")); watch->setToolTip(tr("Animate this value (stopwatch)"));
    auto* key = new QToolButton; key->setAutoRaise(true); key->setText(QString::fromUtf8("◇")); key->setToolTip(tr("Add or remove a keyframe at the current frame"));
    r->addWidget(sp); r->addWidget(watch); r->addWidget(key);
    connect(sp, &QDoubleSpinBox::valueChanged, this, [this, propId](double v) {
        Layer* l = m_s->activeLayer(); if (!l || m_rebuilding) return;
        auto it = l->tracks.constFind(propId);
        if ((it != l->tracks.constEnd() && it->animated()) || m_s->autoKey()) m_s->setProperties(l->id, {{propId, PropValue::of(v)}}, propId);   // keyframe at this frame
        else edit(tr("Effect Setting"), propId, [&](LayerFx& f) { writeFxValue(f, propId, v); });
    });
    connect(watch, &QToolButton::clicked, this, [this, propId](bool on) { setNumberAnimated(propId, on); });
    connect(key, &QToolButton::clicked, this, [this, propId] { if (Layer* l = m_s->activeLayer()) m_s->toggleKeyHere(l->id, propId); });
    m_rows.push_back({propId, sp, watch, key});
    return w;
}

bool EffectsPanel::setNumberAnimated(const QString& propId, bool on) {
    Layer* l = m_s->activeLayer(); Document* d = m_s->doc();
    const PropertyDef* def = findPropertyDef(propId);
    if (!l || !d || !def || l->props.locked) return false;
    auto it = l->tracks.constFind(propId);
    const bool isOn = it != l->tracks.constEnd() && it->animated();
    if (on == isOn) return false;
    const int frame = d->currentFrame();
    const LayerFx now = l->fxAt(frame);
    double v = def->def.c[0];
    if (!readFxValue(now, propId, &v)) return false;
    PropertyTracks t = l->tracks;
    d->undoStack()->beginMacro(on ? tr("Animate %1").arg(l->propertyLabel(propId)) : tr("Stop Animating %1").arg(l->propertyLabel(propId)));
    if (on) {
        PropertyTrack trk; trk.base = PropValue::of(v);
        trk.setKey(*def, frame, PropValue::of(v));
        t.insert(propId, trk);
        d->editTracks(l->id, t, tr("Animate"));
    } else {
        t.remove(propId);
        LayerFx fx = l->fx; writeFxValue(fx, propId, v);                 // the value at this frame becomes the fixed value
        d->setLayerFx(l->id, fx, tr("Stop Animating"));
        d->editTracks(l->id, t, tr("Stop Animating"));
    }
    d->undoStack()->endMacro();
    updateValues();
    return true;
}

void EffectsPanel::updateValues() {
    Layer* l = m_s->activeLayer(); Document* d = m_s->doc();
    if (!l || !d) return;
    const LayerFx now = l->fxAt(d->currentFrame());
    for (const ValueRow& r : m_rows) {
        double v;
        if (!readFxValue(now, r.id, &v)) continue;
        auto it = l->tracks.constFind(r.id);
        const bool animated = it != l->tracks.constEnd() && it->animated();
        if (!r.spin->hasFocus() && r.spin->value() != v) { QSignalBlocker b(r.spin); r.spin->setValue(v); }
        { QSignalBlocker b(r.watch); r.watch->setChecked(animated); }
        r.key->setEnabled(animated);
        r.key->setText(QString::fromUtf8(animated && it->indexOfKey(d->currentFrame()) >= 0 ? "◆" : "◇"));
    }
}

bool EffectsPanel::addEffect(EffectType t) {
    Layer* l = m_s->activeLayer(); if (!l || l->props.locked) return false;
    edit(tr("Add %1").arg(effectInfo(t).label), {}, [&](LayerFx& f) { f.effects << Effect::make(t); });
    rebuild();
    return true;
}
bool EffectsPanel::addMask(const Mask& m) {
    Layer* l = m_s->activeLayer(); if (!l || l->props.locked) return false;
    edit(tr("Add Mask"), {}, [&](LayerFx& f) { Mask k = m; k.name = tr("Mask %1").arg(f.masks.size() + 1); f.masks << k; });
    rebuild();
    return true;
}
bool EffectsPanel::addRectangleMask() {
    if (!m_s->doc()) return false;
    const QSizeF c = m_s->doc()->canvasSize();
    Mask m; m.shape = VectorShape::makeRect(QRectF(c.width() * 0.25, c.height() * 0.25, c.width() * 0.5, c.height() * 0.5)); m.shape.closed = true;
    return addMask(m);
}
bool EffectsPanel::addEllipseMask() {
    if (!m_s->doc()) return false;
    const QSizeF c = m_s->doc()->canvasSize();
    Mask m; m.shape = VectorShape::makeEllipse(QRectF(c.width() * 0.25, c.height() * 0.25, c.width() * 0.5, c.height() * 0.5)); m.shape.closed = true;
    return addMask(m);
}
bool EffectsPanel::addMaskFromSelectedShapes() {
    Layer* l = m_s->activeLayer();
    if (!l || l->type != LayerType::Vector || l->props.locked) return false;
    QVector<Mask> found;
    for (const VectorShape& sh : l->shapes) if (m_s->selectedShapes().contains(sh.id) && sh.nodes.size() >= 3) {
        Mask m; m.shape = sh; m.shape.closed = true; m.name = sh.name.isEmpty() ? QString() : sh.name; found << m;
    }
    if (found.isEmpty()) return false;
    edit(tr("Mask from Shape"), {}, [&](LayerFx& f) { for (Mask m : found) { if (m.name.isEmpty()) m.name = tr("Mask %1").arg(f.masks.size() + 1); f.masks << m; } });
    rebuild();
    return true;
}
bool EffectsPanel::clearMasks() {
    Layer* l = m_s->activeLayer(); if (!l || l->props.locked || l->fx.masks.isEmpty()) return false;
    edit(tr("Remove Masks"), {}, [](LayerFx& f) { f.masks.clear(); });
    rebuild();
    return true;
}
bool EffectsPanel::clearEffects() {
    Layer* l = m_s->activeLayer(); if (!l || l->props.locked || l->fx.effects.isEmpty()) return false;
    edit(tr("Remove Effects"), {}, [](LayerFx& f) { f.effects.clear(); });
    rebuild();
    return true;
}
bool EffectsPanel::setMatte(const QUuid& layer, MatteMode mode) {
    Layer* l = m_s->activeLayer(); if (!l || l->props.locked) return false;
    if (layer == l->id) return false;
    edit(tr("Track Matte"), {}, [&](LayerFx& f) { f.matteLayer = mode == MatteMode::None ? QUuid() : layer; f.matteMode = f.matteLayer.isNull() ? MatteMode::None : mode; });
    rebuild();
    return true;
}

void EffectsPanel::rebuild() {
    if (m_rebuilding) return;
    m_rebuilding = true;
    m_rows.clear();
    while (QLayoutItem* it = m_layout->takeAt(0)) { if (QWidget* w = it->widget()) w->deleteLater(); delete it; }
    Layer* l = m_s->activeLayer();
    m_shownLayer = l ? l->id : QUuid(); m_shown = l ? l->fx : LayerFx();
    if (!l) {
        if (!m_embedded) { auto* lab = new QLabel(tr("Select a layer to give it masks, a track matte or effects.")); lab->setWordWrap(true); m_layout->addWidget(lab); }
        m_rebuilding = false; return;
    }
    // Embedded in the Properties panel (After Effects style): effects, masks and a matte show up only once the layer has them; they are
    // added from the Effect and Layer menus.
    const bool showMatte = !m_embedded || l->fx.matteMode != MatteMode::None;
    const bool showMaskBar = !m_embedded;
    const bool showMasks = !m_embedded || !l->fx.masks.isEmpty();
    const bool showEffects = !m_embedded || !l->fx.effects.isEmpty();
    const bool locked = l->props.locked;
    if (!m_embedded) m_layout->addWidget(new QLabel(tr("<b>%1</b>").arg(l->props.name.toHtmlEscaped())));

    // ---- track matte ----
    if (showMatte) m_layout->addWidget(new QLabel(tr("<b>Track matte</b>")));
    if (showMatte) {
        auto* row = new QHBoxLayout;
        auto* layerBox = new QComboBox; layerBox->addItem(tr("(none)"), QUuid());
        std::function<void(const Layer*)> walk = [&](const Layer* g) {
            for (const auto& c : g->children) { if (c->id != l->id) layerBox->addItem(c->props.name, c->id); if (c->isGroup()) walk(c.get()); }
        };
        walk(m_s->doc()->root());
        const int li = layerBox->findData(l->fx.matteLayer); layerBox->setCurrentIndex(li < 0 ? 0 : li);
        auto* modeBox = new QComboBox; modeBox->addItems({tr("Alpha"), tr("Inverted alpha"), tr("Luma"), tr("Inverted luma")});
        modeBox->setCurrentIndex(l->fx.matteMode == MatteMode::None ? 0 : int(l->fx.matteMode) - 1);
        layerBox->setEnabled(!locked); modeBox->setEnabled(!locked);
        auto apply = [this, layerBox, modeBox] {
            const QUuid id = layerBox->currentData().toUuid();
            setMatte(id, id.isNull() ? MatteMode::None : MatteMode(modeBox->currentIndex() + 1));
        };
        connect(layerBox, &QComboBox::activated, this, apply);
        connect(modeBox, &QComboBox::activated, this, apply);
        row->addWidget(layerBox, 1); row->addWidget(modeBox);
        auto* w = new QWidget; w->setLayout(row); row->setContentsMargins(0, 0, 0, 0); m_layout->addWidget(w);
        auto* hint = new QLabel(tr("The matte layer is not shown by itself; this layer shows only where the matte is opaque (or bright).")); hint->setWordWrap(true); hint->setStyleSheet("color:#8a8f98;");
        m_layout->addWidget(hint);
    }

    // ---- masks ----
    if (showMasks) m_layout->addWidget(new QLabel(tr("<b>Masks</b>")));
    if (showMasks) {
        auto* row = new QHBoxLayout; row->setContentsMargins(0, 0, 0, 0);
        auto mk = [&](const QString& text, std::function<void()> fn) { auto* b = new QPushButton(text); b->setEnabled(!locked); connect(b, &QPushButton::clicked, this, [fn] { fn(); }); row->addWidget(b); };
        mk(tr("Rectangle"), [this] { addRectangleMask(); });
        mk(tr("Ellipse"), [this] { addEllipseMask(); });
        mk(tr("From selected shape"), [this] { addMaskFromSelectedShapes(); });
        auto* w = new QWidget; w->setLayout(row); if (showMaskBar) m_layout->addWidget(w); else delete w;
        for (int i = 0; i < l->fx.masks.size(); ++i) {
            const Mask& m = l->fx.masks[i];
            auto* box = new QFrame; box->setFrameShape(QFrame::StyledPanel);
            auto* bl = new QVBoxLayout(box); bl->setContentsMargins(6, 4, 6, 4);
            auto* top = new QHBoxLayout;
            top->addWidget(new QLabel(m.name.toHtmlEscaped()), 1);
            auto* mode = new QComboBox; mode->addItems({tr("Add"), tr("Subtract"), tr("Intersect")}); mode->setCurrentIndex(int(m.mode));
            connect(mode, &QComboBox::activated, this, [this, i, mode] { edit(tr("Mask Mode"), {}, [&](LayerFx& f) { if (i < f.masks.size()) f.masks[i].mode = MaskMode(mode->currentIndex()); }); });
            auto* inv = new QCheckBox(tr("Invert")); inv->setChecked(m.invert);
            connect(inv, &QCheckBox::toggled, this, [this, i](bool on) { edit(tr("Invert Mask"), {}, [&](LayerFx& f) { if (i < f.masks.size()) f.masks[i].invert = on; }); });
            auto* del = new QToolButton; del->setText(QString::fromUtf8("✕")); del->setToolTip(tr("Remove this mask"));
            connect(del, &QToolButton::clicked, this, [this, i] { edit(tr("Remove Mask"), {}, [&](LayerFx& f) { if (i < f.masks.size()) f.masks.removeAt(i); }); rebuild(); });
            top->addWidget(mode); top->addWidget(inv); top->addWidget(del);
            bl->addLayout(top);
            bl->addWidget(valueRow(tr("Feather"), fxMaskPropId(m, "feather"), 0, 500, 1, tr(" px")));
            bl->addWidget(valueRow(tr("Opacity"), fxMaskPropId(m, "opacity"), 0, 100, 0, " %"));
            box->setEnabled(!locked);
            m_layout->addWidget(box);
        }
    }

    // ---- effects ----
    if (showEffects) m_layout->addWidget(new QLabel(tr("<b>Effects</b> (applied top to bottom, before the masks)")));
    if (showEffects) {
        auto* add = new QToolButton; add->setText(tr("Add effect ▾")); add->setPopupMode(QToolButton::InstantPopup); add->setEnabled(!locked);
        auto* menu = new QMenu(add);
        for (EffectType t : allEffectTypes()) menu->addAction(effectInfo(t).label, this, [this, t] { addEffect(t); });
        add->setMenu(menu);
        if (!m_embedded) m_layout->addWidget(add, 0, Qt::AlignLeft); else delete add;
        for (int i = 0; i < l->fx.effects.size(); ++i) {
            const Effect& e = l->fx.effects[i];
            const EffectInfo& info = effectInfo(e.type);
            auto* box = new QFrame; box->setFrameShape(QFrame::StyledPanel);
            auto* bl = new QVBoxLayout(box); bl->setContentsMargins(6, 4, 6, 4);
            auto* top = new QHBoxLayout;
            auto* on = new QCheckBox(tr(info.label)); on->setChecked(e.enabled);
            connect(on, &QCheckBox::toggled, this, [this, i](bool v) { edit(tr("Toggle Effect"), {}, [&](LayerFx& f) { if (i < f.effects.size()) f.effects[i].enabled = v; }); });
            top->addWidget(on, 1);
            auto* up = new QToolButton; up->setText(QString::fromUtf8("▲")); up->setEnabled(i > 0);
            auto* dn = new QToolButton; dn->setText(QString::fromUtf8("▼")); dn->setEnabled(i + 1 < l->fx.effects.size());
            auto* del = new QToolButton; del->setText(QString::fromUtf8("✕")); del->setToolTip(tr("Remove this effect"));
            connect(up, &QToolButton::clicked, this, [this, i] { edit(tr("Move Effect"), {}, [&](LayerFx& f) { if (i > 0 && i < f.effects.size()) f.effects.swapItemsAt(i, i - 1); }); rebuild(); });
            connect(dn, &QToolButton::clicked, this, [this, i] { edit(tr("Move Effect"), {}, [&](LayerFx& f) { if (i + 1 < f.effects.size()) f.effects.swapItemsAt(i, i + 1); }); rebuild(); });
            connect(del, &QToolButton::clicked, this, [this, i] { edit(tr("Remove Effect"), {}, [&](LayerFx& f) { if (i < f.effects.size()) f.effects.removeAt(i); }); rebuild(); });
            top->addWidget(up); top->addWidget(dn); top->addWidget(del);
            bl->addLayout(top);
            for (int k = 0; k < info.paramCount; ++k) bl->addWidget(valueRow(tr(info.params[k].label), fxEffectPropId(e, k), info.params[k].min, info.params[k].max, 1, QString()));
            if (e.type == EffectType::DropShadow) {
                auto* cb = new QPushButton(tr("Shadow color…"));
                cb->setStyleSheet(QString("QPushButton { border-left: 14px solid %1; }").arg(e.color.name()));
                connect(cb, &QPushButton::clicked, this, [this, i, cb] {
                    Layer* ly = m_s->activeLayer(); if (!ly || i >= ly->fx.effects.size()) return;
                    const QColor c = QColorDialog::getColor(ly->fx.effects[i].color, this, tr("Shadow color"));
                    if (!c.isValid()) return;
                    edit(tr("Shadow Color"), {}, [&](LayerFx& f) { if (i < f.effects.size()) f.effects[i].color = c; });
                    cb->setStyleSheet(QString("QPushButton { border-left: 14px solid %1; }").arg(c.name()));
                });
                bl->addWidget(cb);
            }
            box->setEnabled(!locked);
            m_layout->addWidget(box);
        }
    }
    if (!m_embedded || l->fx.any()) { auto* note = new QLabel(tr("Drag a value to change it. The stopwatch makes it animate: change it on other frames to add keyframes. (Shadow colour and matte settings are not animated.)")); note->setWordWrap(true); note->setStyleSheet("color:#8a8f98;"); m_layout->addWidget(note); }
    m_rebuilding = false;
    updateValues();
    if (!m_embedded) m_layout->addStretch(1);
    m_rebuilding = false;
}

}  // namespace mynd
