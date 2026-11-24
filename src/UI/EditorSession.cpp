#include "UI/EditorSession.h"

#include <QSettings>

namespace mynd {

QString toolName(ToolId t) {
    switch (t) {
    case ToolId::Select: return "Select / Transform";
    case ToolId::DirectSelect: return "Direct Selection";
    case ToolId::Brush: return "Brush";
    case ToolId::Pencil: return "Pencil";
    case ToolId::Eraser: return "Eraser";
    case ToolId::Pen: return "Pen (Bézier)";
    case ToolId::Line: return "Line";
    case ToolId::Rectangle: return "Rectangle";
    case ToolId::Ellipse: return "Ellipse";
    case ToolId::Polygon: return "Polygon";
    case ToolId::Bucket: return "Paint Bucket";
    case ToolId::Gradient: return "Gradient";
    case ToolId::Eyedropper: return "Eyedropper";
    case ToolId::Hand: return "Hand";
    case ToolId::Zoom: return "Zoom";
    case ToolId::Smudge: return "Smudge";
    case ToolId::FreeTransform: return "Free Transform";
    case ToolId::LayerTransform: return "Transform Layer";
    case ToolId::Text: return "Text";
    }
    return {};
}

EditorSession::EditorSession(QObject* parent) : QObject(parent) {
    m_brush = BrushSettings{};
    m_pencil = BrushSettings{};
    m_pencil.size = 3; m_pencil.hardness = 1.0; m_pencil.antialias = false;
    m_pencil.pressureSize = false; m_pencil.spacing = 0.2;
    m_eraser = BrushSettings{};
    m_eraser.size = 40; m_eraser.hardness = 0.9;
    m_playback = new Playback(this);
    {   // onion skin preferences persist between runs
        QSettings st(QSettings::NativeFormat, QSettings::UserScope, "MyndCraft", "MyndCraft");
        m_onion.enabled = st.value("onion/enabled", false).toBool();
        m_onion.before = qBound(0, st.value("onion/before", 2).toInt(), 10);
        m_onion.after = qBound(0, st.value("onion/after", 1).toInt(), 10);
        m_onion.opacity = qBound(0.05, st.value("onion/opacity", 0.5).toDouble(), 1.0);
        const QColor b(st.value("onion/beforeColor", "#ff4646").toString()), a(st.value("onion/afterColor", "#3cc85a").toString());
        if (b.isValid()) m_onion.beforeColor = b;
        if (a.isValid()) m_onion.afterColor = a;
        m_onion.includeHeld = st.value("onion/includeHeld", false).toBool();
        m_onion.activeLayerOnly = st.value("onion/activeLayerOnly", true).toBool();
    }
    m_smudge.size = 30; m_smudge.hardness = 0.6; m_smudge.flow = 0.6; m_smudge.spacing = 0.08; m_smudge.pressureSize = false;
}

void EditorSession::setDocument(Document* d) {
    m_doc = d;
    m_playback->setDocument(d);
    clearFrameSelection();
    if (d) connect(d, &Document::frameChanged, this, [this] { if (!m_selInternal) clearFrameSelection(); });
    m_selected.clear();
    m_keySel.clear();
    m_activeLayer = {};
    if (d) {
        connect(d, &Document::tracksChanged, this, [this] {
            // keep the keyframe selection valid when keys disappear (delete / undo)
            QVector<KeyRef> keep;
            for (const KeyRef& k : m_keySel) {
                const Layer* l = m_doc ? m_doc->findLayer(k.layer) : nullptr;
                auto it = l ? l->tracks.constFind(k.prop) : PropertyTracks::const_iterator();
                if (l && it != l->tracks.constEnd() && it->indexOfKey(k.frame) >= 0) keep << k;
            }
            if (keep.size() != m_keySel.size()) { m_keySel = keep; emit keySelectionChanged(); }
        });
        // Activate the top-most non-group layer.
        Layer* pick = nullptr;
        Document::forEachLayer(d->root(), [&](Layer* l) { if (!l->isGroup()) pick = l; });
        if (pick) m_activeLayer = pick->id;
        // Keep the active layer valid when layers disappear (delete / undo).
        connect(d, &Document::layerTreeChanged, this, [this] {
            if (m_doc && !m_doc->findLayer(m_activeLayer)) {
                Layer* pick = nullptr;
                Document::forEachLayer(m_doc->root(), [&](Layer* l) { pick = l; });
                m_activeLayer = pick ? pick->id : QUuid();
                m_selected.clear();
                emit activeLayerChanged(m_activeLayer);
                emit selectionChanged();
            }
        });
    }
    emit documentChanged();
    emit activeLayerChanged(m_activeLayer);
    emit selectionChanged();
}

void EditorSession::setActiveLayer(const QUuid& id) {
    if (id == m_activeLayer) return;
    m_activeLayer = id;
    if (!m_selLayer.isNull() && m_selLayer != id) clearFrameSelection();
    m_selected.clear();
    emit activeLayerChanged(id);
    emit selectionChanged();
}

QUuid EditorSession::addLayerAboveActive(LayerType t, const QString& name) {
    if (!m_doc) return {};
    QUuid parent; int idx = -1;
    if (Layer* a = activeLayer()) {
        if (a->isGroup()) { parent = a->id; idx = -1; }
        else {
            parent = a->parent && a->parent != m_doc->root() ? a->parent->id : QUuid();
            idx = m_doc->indexInParent(a) + 1;
        }
    }
    const QUuid id = m_doc->addLayer(t, name, parent, idx);
    setActiveLayer(id);
    return id;
}

QUuid EditorSession::addTextLayerAboveActive(const TextContent& content, const QString& name) {
    if (!m_doc) return {};
    QUuid parent; int idx = -1;
    if (Layer* a = activeLayer()) {
        if (a->isGroup()) parent = a->id;
        else { parent = a->parent && a->parent != m_doc->root() ? a->parent->id : QUuid(); idx = m_doc->indexInParent(a) + 1; }
    }
    const QUuid id = m_doc->addTextLayer(content, name, parent, idx);
    setActiveLayer(id);
    return id;
}

QUuid EditorSession::addImageLayerAboveActive(const QImage& img, const QString& name) {
    if (!m_doc) return {};
    QUuid parent; int idx = -1;
    if (Layer* a = activeLayer()) {
        if (a->isGroup()) parent = a->id;
        else { parent = a->parent && a->parent != m_doc->root() ? a->parent->id : QUuid(); idx = m_doc->indexInParent(a) + 1; }
    }
    const QUuid id = m_doc->addImageLayer(img, name, parent, idx);
    if (!id.isNull()) setActiveLayer(id);
    return id;
}

void EditorSession::applyTextStyle(const TextContent& style) {
    const TextContent old = m_textStyle;
    m_textStyle = style;
    m_textStyle.text.clear();
    m_textStyle.pos = {};
    Layer* l = activeLayer();
    if (m_doc && l && l->type == LayerType::Text && !l->props.locked) {
        TextContent c = l->textContent;
        c.family = style.family; c.sizePx = style.sizePx; c.bold = style.bold; c.italic = style.italic;
        c.color = style.color; c.tracking = style.tracking; c.align = style.align; c.boxWidth = style.boxWidth;
        m_doc->setTextContent(l->id, c, tr("Change Text Style"), "text-style");
    }
    Q_UNUSED(old);
    emit textStyleChanged();
}

void EditorSession::setOnion(const Compositor::OnionSkin& o) {
    m_onion = o;
    m_onion.activeLayer = {};
    QSettings st(QSettings::NativeFormat, QSettings::UserScope, "MyndCraft", "MyndCraft");
    st.setValue("onion/enabled", o.enabled);
    st.setValue("onion/before", o.before);
    st.setValue("onion/after", o.after);
    st.setValue("onion/opacity", o.opacity);
    st.setValue("onion/beforeColor", o.beforeColor.name());
    st.setValue("onion/afterColor", o.afterColor.name());
    st.setValue("onion/includeHeld", o.includeHeld);
    st.setValue("onion/activeLayerOnly", o.activeLayerOnly);
    emit onionChanged();
    if (m_doc) m_doc->emitContentChanged({});
}

Layer* EditorSession::activeAnimationLayer() const {
    Layer* l = activeLayer();
    return l && l->type == LayerType::Animation ? l : nullptr;
}

bool EditorSession::editActiveAnimation(const QString& undoText, const std::function<bool(AnimationState&)>& fn) {
    Layer* l = activeAnimationLayer();
    if (!m_doc || !l) { emit message(tr("Select an animation layer first.")); return false; }
    if (l->props.locked) { emit message(tr("The active layer is locked.")); return false; }
    AnimationState s = l->anim;
    if (!fn(s)) return false;
    s.normalize();
    if (s == l->anim) return false;
    m_doc->editAnimation(l->id, s, undoText);
    return true;
}

QUuid EditorSession::newDrawing(bool copyVisible) {
    if (!m_doc) return {};
    Layer* l = activeAnimationLayer();
    if (!l) {
        addLayerAboveActive(LayerType::Animation, tr("Animation Layer"));
        l = activeAnimationLayer();
    }
    if (!l || l->props.locked) { emit message(tr("The active layer is locked.")); return {}; }
    return m_doc->createDrawing(l->id, m_doc->currentFrame(), copyVisible);
}

QUuid EditorSession::newDrawingOnNextFrame(bool copyVisible) {
    if (!m_doc) return {};
    Layer* l = activeAnimationLayer();
    if (!l) { emit message(tr("Select an animation layer first.")); return {}; }
    if (l->props.locked) { emit message(tr("The active layer is locked.")); return {}; }
    const int next = m_doc->currentFrame() + 1;
    if (next >= m_doc->durationFrames()) m_doc->setDuration(next + 1);   // straight-ahead work grows the scene
    const int cur = m_doc->currentFrame();
    m_doc->setCurrentFrame(next);
    return m_doc->createDrawing(l->id, next, copyVisible, cur);   // optionally start from the drawing we just left
}

void EditorSession::setFrameSelection(const QUuid& layer, int first, int last) {
    if (!m_doc) return;
    if (!m_keySel.isEmpty()) { m_keySel.clear(); emit keySelectionChanged(); }
    const int hi = m_doc->durationFrames() - 1;
    first = qBound(0, first, hi); last = qBound(0, last, hi);
    m_selLayer = layer;
    m_selFirst = std::min(first, last);
    m_selLast = std::max(first, last);
    m_selInternal = true;
    if (m_doc->currentFrame() != last) m_doc->setCurrentFrame(last);
    m_selInternal = false;
    emit frameSelectionChanged();
}

void EditorSession::clearFrameSelection() {
    if (m_selLayer.isNull() && m_selFirst < 0) return;
    m_selLayer = {};
    m_selFirst = m_selLast = -1;
    emit frameSelectionChanged();
}

void EditorSession::selectedFrameRange(int* first, int* last) const {
    if (hasFrameSelection() && m_selLayer == m_activeLayer) { *first = m_selFirst; *last = m_selLast; return; }
    *first = *last = m_doc ? m_doc->currentFrame() : 0;
}

bool EditorSession::copyFrames(bool cut) {
    Layer* l = activeAnimationLayer();
    if (!m_doc || !l) { emit message(tr("Select an animation layer to copy frames.")); return false; }
    if (cut && l->props.locked) { emit message(tr("The active layer is locked.")); return false; }
    int a, b;
    selectedFrameRange(&a, &b);
    FrameClip clip;
    clip.length = b - a + 1;
    clip.sourceLayer = l->id;
    QSet<QUuid> used;
    for (const Exposure& e : l->anim.exposures) {
        const int s = std::max(e.start, a), t = std::min(e.end(), b + 1);
        if (t <= s) continue;
        Exposure c = e; c.start = s - a; c.length = t - s;
        clip.exposures.append(c);
        if (!e.drawing.isNull()) used.insert(e.drawing);
    }
    for (const DrawingMeta& dm : l->anim.drawings) {
        if (!used.contains(dm.id)) continue;
        clip.drawings.append(dm);
        if (const QImage* img = l->drawingImage(dm.id)) clip.images[dm.id] = img->copy();   // deep copy: later edits must not change the clip
    }
    m_clip = clip;
    m_keyClipActive = false;
    emit clipChanged();
    if (cut) {
        editActiveAnimation(tr("Cut Frames"), [&](AnimationState& s) { s.clearRange(a, b - a + 1); return true; });
        clearFrameSelection();
    }
    return true;
}

bool EditorSession::pasteFrames(bool linked) {
    Layer* l = activeAnimationLayer();
    if (!m_doc || !l) { emit message(tr("Select an animation layer to paste frames into.")); return false; }
    if (l->props.locked) { emit message(tr("The active layer is locked.")); return false; }
    if (m_clip.isEmpty()) { emit message(tr("Nothing copied yet.")); return false; }
    const int at = m_doc->currentFrame();
    AnimationState s = l->anim;
    QHash<QUuid, QUuid> map;          // clip drawing id -> drawing id on this layer
    for (const DrawingMeta& dm : m_clip.drawings) {
        if (linked && m_clip.sourceLayer == l->id && s.drawing(dm.id)) { map[dm.id] = dm.id; continue; }
        const QUuid id = QUuid::createUuid();
        s.drawings.push_back({id, s.nextDrawingName()});
        QImage img = m_clip.images.value(dm.id);
        l->drawingImages[id] = img.isNull() ? m_doc->newRasterImage() : img.copy();   // inert until exposed; kept for undo/redo
        map[dm.id] = id;
    }
    s.clearRange(at, m_clip.length);
    for (const Exposure& e : m_clip.exposures) {
        Exposure c = e;
        c.start = at + e.start;
        c.drawing = e.drawing.isNull() ? QUuid() : map.value(e.drawing);
        s.exposures.append(c);
    }
    s.normalize();
    if (s == l->anim) return false;
    m_doc->editAnimation(l->id, s, linked ? tr("Paste Frames (Linked)") : tr("Paste Frames"));
    setFrameSelection(l->id, at, at + m_clip.length - 1);
    if (m_doc->currentFrame() != at) { m_selInternal = true; m_doc->setCurrentFrame(at); m_selInternal = false; }
    return true;
}

bool EditorSession::gotoDrawing(int direction) {
    Layer* l = activeAnimationLayer();
    if (!m_doc || !l) return false;
    const int f = l->anim.neighborStart(m_doc->currentFrame(), direction);
    if (f < 0) return false;
    m_doc->setCurrentFrame(f);
    return true;
}

Layer* EditorSession::editableLayer(LayerType t, bool autoCreate) {
    if (!m_doc) return nullptr;
    Layer* l = activeLayer();
    // An animation layer is a pixel layer whose bitmap depends on the frame.
    if (l && (l->type == t || (t == LayerType::Raster && l->type == LayerType::Animation))) {
        if (l->props.locked) { emit message(tr("The active layer is locked.")); return nullptr; }
        if (!l->props.visible) { emit message(tr("The active layer is hidden.")); return nullptr; }
        if (l->type == LayerType::Animation && !m_doc->editImage(l)) {
            // Drawing on a frame with no drawing creates one there (undoable).
            if (!autoCreate) { emit message(tr("There is no drawing at this frame.")); return nullptr; }
            if (m_doc->createDrawing(l->id, m_doc->currentFrame(), false).isNull()) return nullptr;
        }
        return l;
    }
    if (!autoCreate) {
        emit message(t == LayerType::Raster ? tr("Select a raster layer to use this tool.")
                                            : tr("Select a vector layer to use this tool."));
        return nullptr;
    }
    const QUuid id = addLayerAboveActive(t, t == LayerType::Raster ? tr("Raster Layer") : tr("Vector Layer"));
    return m_doc->findLayer(id);
}

void EditorSession::setSelectedShapes(const QSet<QUuid>& s, bool pullStyle) {
    m_selected = s;
    if (pullStyle && m_doc && !s.isEmpty()) {
        if (Layer* l = activeLayer()) {
            for (const auto& sh : l->shapes) {
                if (s.contains(sh.id)) {
                    m_style.fillEnabled = sh.fill.type != Paint::None;
                    if (sh.fill.type == Paint::Solid) m_style.fill = sh.fill.color;
                    m_style.strokeEnabled = sh.stroke.enabled;
                    m_style.stroke = sh.stroke.color;
                    m_style.strokeWidth = sh.stroke.width;
                    emit styleChanged();
                    break;
                }
            }
        }
    }
    emit selectionChanged();
}

QSet<QUuid> EditorSession::expandGroups(const Layer& l, const QSet<QUuid>& ids) const {
    QSet<QUuid> groups, out = ids;
    for (const auto& s : l.shapes) if (ids.contains(s.id) && !s.groupId.isNull()) groups.insert(s.groupId);
    for (const auto& s : l.shapes) if (!s.groupId.isNull() && groups.contains(s.groupId)) out.insert(s.id);
    return out;
}

void EditorSession::setTool(ToolId t) {
    if (t == m_tool) return;
    m_tool = t;
    emit toolChanged(t);
}

void EditorSession::setForeground(const QColor& c) { m_fg = c; emit colorsChanged(); }
void EditorSession::setBackground(const QColor& c) { m_bg = c; emit colorsChanged(); }
void EditorSession::swapColors() { std::swap(m_fg, m_bg); emit colorsChanged(); }

BrushSettings& EditorSession::brushFor(ToolId t) {
    if (t == ToolId::Pencil) return m_pencil;
    if (t == ToolId::Eraser) return m_eraser;
    if (t == ToolId::Smudge) return m_smudge;
    return m_brush;
}
BrushSettings EditorSession::brush(ToolId t) const { return const_cast<EditorSession*>(this)->brushFor(t); }

VectorShape EditorSession::styled(VectorShape s) const {
    s.fill.type = (m_style.fillEnabled && s.closed) ? Paint::Solid : Paint::None;
    s.fill.color = m_style.fill;
    s.stroke.enabled = m_style.strokeEnabled;
    s.stroke.color = m_style.stroke;
    s.stroke.width = m_style.strokeWidth;
    return s;
}

void EditorSession::setStyle(const VectorStyle& st, bool apply) {
    m_style = st;
    if (apply && m_doc && !m_selected.isEmpty()) {
        Layer* l = activeLayer();
        if (l && l->type == LayerType::Vector && !l->props.locked) {
            ShapeList after = l->shapes;
            for (auto& s : after) {
                if (!m_selected.contains(s.id)) continue;
                if (!st.fillEnabled) s.fill.type = Paint::None;
                else { s.fill.type = Paint::Solid; s.fill.color = st.fill; }
                s.stroke.enabled = st.strokeEnabled;
                s.stroke.color = st.stroke;
                s.stroke.width = st.strokeWidth;
            }
            // Merge consecutive style tweaks into one undo step by reusing a single text.
            m_doc->setShapes(l->id, after, tr("Change Style"));
        }
    }
    emit styleChanged();
}

// ---------------------------------------------------------------- property animation
void EditorSession::setAutoKey(bool on) {
    if (on == m_autoKey) return;
    m_autoKey = on;
    emit autoKeyChanged();
}

PropValue EditorSession::propertyValue(const QUuid& layer, const QString& prop) const {
    const PropertyDef* d = findPropertyDef(prop);
    if (!d) return {};
    const Layer* l = m_doc ? m_doc->findLayer(layer) : nullptr;
    return l ? valueOf(l->tracks, *d, m_doc->currentFrame()) : d->def;
}

static Layer* editableTracksLayer(EditorSession* s, const QUuid& id) {
    Layer* l = s->doc() ? s->doc()->findLayer(id) : nullptr;
    if (!l) return nullptr;
    if (l->props.locked) { emit s->message(QObject::tr("The layer is locked.")); return nullptr; }
    return l;
}

bool EditorSession::setProperties(const QUuid& layer, const QVector<QPair<QString, PropValue>>& values, const QString& mergeKey) {
    Layer* l = editableTracksLayer(this, layer);
    if (!l || values.isEmpty()) return false;
    PropertyTracks t = l->tracks;
    QString name;
    for (const auto& [id, v] : values) {
        const PropertyDef* d = findPropertyDef(id);
        if (!d) continue;
        setValueAt(t, id, m_doc->currentFrame(), v, m_autoKey);
        name = name.isEmpty() ? d->label : tr("Properties");
    }
    if (name.isEmpty() || t == l->tracks) return false;
    m_doc->editTracks(layer, t, tr("Change %1").arg(name), mergeKey);
    return true;
}

bool EditorSession::toggleAnimated(const QUuid& layer, const QString& prop) {
    Layer* l = editableTracksLayer(this, layer);
    const PropertyDef* d = findPropertyDef(prop);
    if (!l || !d) return false;
    PropertyTracks t = l->tracks;
    const bool animated = t.contains(prop) && t[prop].animated();
    if (animated) disableAnimation(t, prop, m_doc->currentFrame()); else enableAnimation(t, prop, m_doc->currentFrame());
    m_doc->editTracks(layer, t, animated ? tr("Stop Animating %1").arg(d->label) : tr("Animate %1").arg(d->label));
    return true;
}

bool EditorSession::toggleKeyHere(const QUuid& layer, const QString& prop) {
    Layer* l = editableTracksLayer(this, layer);
    const PropertyDef* d = findPropertyDef(prop);
    if (!l || !d) return false;
    PropertyTracks t = l->tracks;
    const int f = m_doc->currentFrame();
    const PropValue cur = valueOf(t, *d, f);
    if (!t.contains(prop)) { PropertyTrack nt; nt.base = d->def; t.insert(prop, nt); }
    PropertyTrack& trk = t[prop];
    if (trk.indexOfKey(f) >= 0) {
        if (trk.keys.size() == 1) disableAnimation(t, prop, f);     // last key: the property becomes static again
        else trk.removeKey(f);
        m_doc->editTracks(layer, t, tr("Remove Keyframe"));
    } else {
        trk.setKey(*d, f, cur);
        m_doc->editTracks(layer, t, tr("Add Keyframe"));
    }
    return true;
}

bool EditorSession::gotoKey(int direction) {
    Layer* l = activeLayer();
    if (!m_doc || !l || direction == 0) return false;
    const int cur = m_doc->currentFrame();
    int best = -1;
    for (auto it = l->tracks.begin(); it != l->tracks.end(); ++it)
        for (const Keyframe& k : it->keys) {
            if (direction > 0 && k.frame > cur && (best < 0 || k.frame < best)) best = k.frame;
            if (direction < 0 && k.frame < cur && k.frame > best) best = k.frame;
        }
    if (best < 0) return false;
    m_doc->setCurrentFrame(best);
    return true;
}

void EditorSession::setKeySelection(const QVector<KeyRef>& keys) {
    if (keys == m_keySel) return;
    m_keySel = keys;
    if (!keys.isEmpty()) clearFrameSelection();            // frames and keyframes are never selected at once
    emit keySelectionChanged();
}

bool EditorSession::isKeySelected(const QUuid& layer, const QString& prop, int frame) const {
    return m_keySel.contains(KeyRef{layer, prop, frame});
}

namespace {
// Selected keys grouped by layer, then property.
QMap<QUuid, QMap<QString, QVector<int>>> groupKeys(const QVector<KeyRef>& sel) {
    QMap<QUuid, QMap<QString, QVector<int>>> g;
    for (const KeyRef& k : sel) g[k.layer][k.prop].push_back(k.frame);
    return g;
}
}  // namespace

bool EditorSession::deleteSelectedKeys() {
    if (!m_doc || m_keySel.isEmpty()) return false;
    const auto groups = groupKeys(m_keySel);
    bool any = false;
    m_doc->undoStack()->beginMacro(tr("Delete Keyframes"));
    for (auto lg = groups.begin(); lg != groups.end(); ++lg) {
        Layer* l = editableTracksLayer(this, lg.key());
        if (!l) continue;
        PropertyTracks t = l->tracks;
        for (auto pg = lg->begin(); pg != lg->end(); ++pg) {
            if (!t.contains(pg.key())) continue;
            PropertyTrack& tr = t[pg.key()];
            const PropertyDef* d = findPropertyDef(pg.key());
            // Removing the last key leaves the property static at the value it had at that key.
            const PropValue last = d && !tr.keys.isEmpty() ? tr.keys.last().value : tr.base;
            for (int f : pg.value()) any |= tr.removeKey(f);
            if (tr.keys.isEmpty()) tr.base = last;
        }
        m_doc->editTracks(lg.key(), t, tr("Delete Keyframes"));
    }
    m_doc->undoStack()->endMacro();
    clearKeySelection();
    return any;
}

bool EditorSession::moveSelectedKeys(int delta) {
    if (!m_doc || m_keySel.isEmpty() || delta == 0) return false;
    int minFrame = INT_MAX;
    for (const KeyRef& k : m_keySel) minFrame = qMin(minFrame, k.frame);
    delta = qMax(delta, -minFrame);                       // cannot go before frame 0
    if (delta == 0) return false;
    const auto groups = groupKeys(m_keySel);
    QVector<KeyRef> moved;
    bool any = false;
    m_doc->undoStack()->beginMacro(tr("Move Keyframes"));
    for (auto lg = groups.begin(); lg != groups.end(); ++lg) {
        Layer* l = editableTracksLayer(this, lg.key());
        if (!l) continue;
        PropertyTracks t = l->tracks;
        for (auto pg = lg->begin(); pg != lg->end(); ++pg) {
            if (!t.contains(pg.key())) continue;
            PropertyTrack& tr = t[pg.key()];
            QVector<Keyframe> lift;
            for (int f : pg.value()) { const int i = tr.indexOfKey(f); if (i >= 0) { lift.push_back(tr.keys[i]); } }
            for (const Keyframe& k : lift) tr.removeKey(k.frame);
            for (Keyframe k : lift) {
                k.frame += delta;
                tr.removeKey(k.frame);                    // lands on an unselected key: replaces it
                auto it = std::lower_bound(tr.keys.begin(), tr.keys.end(), k.frame, [](const Keyframe& a, int fr) { return a.frame < fr; });
                tr.keys.insert(it, k);
                moved.push_back({lg.key(), pg.key(), k.frame});
                any = true;
            }
        }
        m_doc->editTracks(lg.key(), t, tr("Move Keyframes"));
    }
    m_doc->undoStack()->endMacro();
    setKeySelection(moved);
    return any;
}

bool EditorSession::easeSelectedKeys(EasePreset preset, bool incoming, bool outgoing) {
    if (!m_doc || m_keySel.isEmpty()) return false;
    const auto groups = groupKeys(m_keySel);
    bool any = false;
    m_doc->undoStack()->beginMacro(tr("Keyframe Interpolation"));
    for (auto lg = groups.begin(); lg != groups.end(); ++lg) {
        Layer* l = editableTracksLayer(this, lg.key());
        if (!l) continue;
        PropertyTracks t = l->tracks;
        for (auto pg = lg->begin(); pg != lg->end(); ++pg) {
            if (!t.contains(pg.key())) continue;
            for (int f : pg.value()) any |= t[pg.key()].applyEase(f, preset, incoming, outgoing);
        }
        m_doc->editTracks(lg.key(), t, tr("Keyframe Interpolation"));
    }
    m_doc->undoStack()->endMacro();
    return any;
}

bool EditorSession::copyKeys(bool cut) {
    if (!m_doc || m_keySel.isEmpty()) { emit message(tr("Select keyframes to copy.")); return false; }
    int minFrame = INT_MAX;
    for (const KeyRef& k : m_keySel) minFrame = qMin(minFrame, k.frame);
    KeyClip clip;
    for (const KeyRef& r : m_keySel) {
        const Layer* l = m_doc->findLayer(r.layer);
        if (!l || !l->tracks.contains(r.prop)) continue;
        const PropertyTrack& tr = l->tracks[r.prop];
        const int i = tr.indexOfKey(r.frame);
        if (i < 0) continue;
        Keyframe k = tr.keys[i];
        k.frame -= minFrame;
        QVector<Keyframe>& v = clip.keys[r.prop];
        const auto it = std::find_if(v.begin(), v.end(), [&](const Keyframe& e) { return e.frame == k.frame; });
        if (it != v.end()) *it = k; else v.push_back(k);
    }
    if (clip.isEmpty()) return false;
    for (auto& v : clip.keys) std::sort(v.begin(), v.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
    m_keyClip = clip;
    m_keyClipActive = true;
    emit keyClipChanged();
    if (cut) deleteSelectedKeys();
    return true;
}

bool EditorSession::pasteKeys() {
    Layer* l = activeLayer();
    if (!m_doc || !l) { emit message(tr("Select a layer to paste keyframes onto.")); return false; }
    if (m_keyClip.isEmpty()) { emit message(tr("Copy keyframes first.")); return false; }
    l = editableTracksLayer(this, l->id);
    if (!l) return false;
    PropertyTracks t = l->tracks;
    const int at = m_doc->currentFrame();
    QVector<KeyRef> sel;
    for (auto it = m_keyClip.keys.begin(); it != m_keyClip.keys.end(); ++it) {
        const PropertyDef* d = findPropertyDef(it.key());
        if (!d) continue;
        if (!t.contains(it.key())) { PropertyTrack nt; nt.base = d->def; t.insert(it.key(), nt); }
        PropertyTrack& tr = t[it.key()];
        for (Keyframe k : it.value()) {
            k.frame += at;
            tr.removeKey(k.frame);
            auto pos = std::lower_bound(tr.keys.begin(), tr.keys.end(), k.frame, [](const Keyframe& a, int fr) { return a.frame < fr; });
            tr.keys.insert(pos, k);
            sel.push_back({l->id, it.key(), k.frame});
        }
    }
    if (sel.isEmpty()) return false;
    m_doc->editTracks(l->id, t, tr("Paste Keyframes"));
    setKeySelection(sel);
    return true;
}

}  // namespace mynd
