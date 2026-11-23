#include "UI/EditorSession.h"

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
}

void EditorSession::setDocument(Document* d) {
    m_doc = d;
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

Layer* EditorSession::editableLayer(LayerType t, bool autoCreate) {
    if (!m_doc) return nullptr;
    Layer* l = activeLayer();
    if (l && l->type == t) {
        if (l->props.locked) { emit message(tr("The active layer is locked.")); return nullptr; }
        if (!l->props.visible) { emit message(tr("The active layer is hidden.")); return nullptr; }
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
