#include "UI/EditorSession.h"

#include <QSettings>

namespace mynd {

QString toolName(ToolId t) {
    switch (t) {
    case ToolId::Select: return "Selection";
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
    m_selected.clear();
    m_activeLayer = {};
    if (d) {
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

}  // namespace mynd
