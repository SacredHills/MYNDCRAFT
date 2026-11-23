#include "Scene/Document.h"

#include <QPainter>

#include "Core/UndoCommands.h"

namespace mynd {

Document::Document(const CompositionSettings& s, QObject* parent) : QObject(parent), m_comp(s) {
    m_comp.width = qBound(1, m_comp.width, kMaxCanvasDim);
    m_comp.height = qBound(1, m_comp.height, kMaxCanvasDim);
    m_root = std::make_shared<Layer>(LayerType::Group, "Root");
    m_undo.setUndoLimit(200);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool) {
        emit modifiedChanged(isModified());
    });
    connect(&m_undo, &QUndoStack::indexChanged, this, [this](int) { emit modifiedChanged(isModified()); });
}
Document::~Document() = default;

void Document::setRoot(LayerPtr r) {
    m_root = std::move(r);
    fixParents(m_root.get());
    m_undo.clear();
    emitTreeChanged();
    emitContentChanged({});
}

void Document::fixParents(Layer* g) {
    for (auto& c : g->children) { c->parent = g; fixParents(c.get()); }
}

void Document::forEachLayer(const Layer* g, const std::function<void(Layer*)>& fn) {
    for (const auto& c : g->children) { fn(c.get()); forEachLayer(c.get(), fn); }
}

int Document::layerCount() const {
    int n = 0;
    forEachLayer(m_root.get(), [&](Layer*) { ++n; });
    return n;
}

static LayerPtr findIn(const LayerPtr& g, const QUuid& id) {
    for (const auto& c : g->children) {
        if (c->id == id) return c;
        if (c->isGroup()) if (auto r = findIn(c, id)) return r;
    }
    return nullptr;
}
LayerPtr Document::findLayerPtr(const QUuid& id) const {
    if (id.isNull()) return nullptr;
    return findIn(m_root, id);
}
Layer* Document::findLayer(const QUuid& id) const { return findLayerPtr(id).get(); }

int Document::indexInParent(const Layer* l) const {
    if (!l || !l->parent) return -1;
    const auto& v = l->parent->children;
    for (size_t i = 0; i < v.size(); ++i) if (v[i].get() == l) return int(i);
    return -1;
}

QImage Document::newRasterImage() const {
    QImage img(canvasSize(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

void Document::markSaved() { m_undo.setClean(); m_extraDirty = false; emit modifiedChanged(false); }
void Document::markDirty() { m_extraDirty = true; emit modifiedChanged(true); }

// ---------------- raw ops ----------------
void Document::rawInsert(const QUuid& parentId, int index, LayerPtr layer) {
    Layer* parent = parentId.isNull() ? m_root.get() : findLayer(parentId);
    if (!parent || !parent->isGroup()) parent = m_root.get();
    if (index < 0 || index > int(parent->children.size())) index = int(parent->children.size());
    layer->parent = parent;
    fixParents(layer.get());
    parent->children.insert(parent->children.begin() + index, std::move(layer));
    emitTreeChanged();
    emitContentChanged({});
}

LayerPtr Document::rawRemove(const QUuid& id, QUuid* parentId, int* index, bool notify) {
    LayerPtr l = findLayerPtr(id);
    if (!l || !l->parent) return nullptr;
    Layer* p = l->parent;
    const int idx = indexInParent(l.get());
    if (parentId) *parentId = p == m_root.get() ? QUuid() : p->id;
    if (index) *index = idx;
    p->children.erase(p->children.begin() + idx);
    l->parent = nullptr;
    if (notify) { emitTreeChanged(); emitContentChanged({}); }
    return l;
}

void Document::emitTreeChanged() { emit layerTreeChanged(); }
void Document::emitContentChanged(const QRect& r) { emit contentChanged(r); }
void Document::emitPropsChanged(const QUuid& id) { emit layerPropsChanged(id); }

// ---------------- undoable edits ----------------
QUuid Document::addLayer(LayerType type, const QString& name, QUuid parentId, int index) {
    auto l = std::make_shared<Layer>(type, name);
    if (type == LayerType::Raster) l->raster = newRasterImage();
    const QUuid id = l->id;
    m_undo.push(new AddLayerCommand(this, parentId, index, l));
    return id;
}

void Document::removeLayer(const QUuid& id) {
    if (findLayer(id)) m_undo.push(new RemoveLayerCommand(this, id));
}

QUuid Document::duplicateLayer(const QUuid& id) {
    LayerPtr src = findLayerPtr(id);
    if (!src || !src->parent) return {};
    LayerPtr c = src->cloneWithNewIds();
    c->props.name += " copy";
    const QUuid parentId = src->parent == m_root.get() ? QUuid() : src->parent->id;
    const int idx = indexInParent(src.get()) + 1;
    auto* cmd = new AddLayerCommand(this, parentId, idx, c);
    cmd->setText(tr("Duplicate Layer"));
    m_undo.push(cmd);
    return c->id;
}

void Document::moveLayer(const QUuid& id, const QUuid& newParent, int newIndex) {
    Layer* l = findLayer(id);
    if (!l) return;
    // Refuse to move a group into itself or its descendants.
    if (!newParent.isNull()) {
        for (Layer* p = findLayer(newParent); p; p = p->parent)
            if (p == l) return;
    }
    // Index is expressed against the destination list *after* removal of the layer.
    m_undo.push(new MoveLayerCommand(this, id, newParent, newIndex));
}

QUuid Document::groupLayer(const QUuid& id) {
    LayerPtr l = findLayerPtr(id);
    if (!l || !l->parent) return {};
    const QUuid parentId = l->parent == m_root.get() ? QUuid() : l->parent->id;
    const int idx = indexInParent(l.get());
    auto g = std::make_shared<Layer>(LayerType::Group, "Group");
    const QUuid gid = g->id;
    m_undo.beginMacro(tr("Group Layer"));
    m_undo.push(new AddLayerCommand(this, parentId, idx + 1, g));
    m_undo.push(new MoveLayerCommand(this, id, gid, 0));
    m_undo.endMacro();
    return gid;
}

void Document::ungroupLayer(const QUuid& groupId) {
    LayerPtr g = findLayerPtr(groupId);
    if (!g || !g->isGroup() || !g->parent) return;
    const QUuid parentId = g->parent == m_root.get() ? QUuid() : g->parent->id;
    int idx = indexInParent(g.get());
    std::vector<QUuid> kids;
    for (const auto& c : g->children) kids.push_back(c->id);
    m_undo.beginMacro(tr("Ungroup"));
    // Move each child to just above the group (keeping order), then delete the empty group.
    int insertAt = idx + 1;
    for (const QUuid& k : kids) m_undo.push(new MoveLayerCommand(this, k, parentId, insertAt++));
    m_undo.push(new RemoveLayerCommand(this, groupId));
    m_undo.endMacro();
}

void Document::setLayerProps(const QUuid& id, const LayerProps& p, const QString& mergeKey) {
    Layer* l = findLayer(id);
    if (!l || l->props == p) return;
    m_undo.push(new LayerPropsCommand(this, id, l->props, p, mergeKey));
}

void Document::setShapes(const QUuid& layerId, const ShapeList& after, const QString& text,
                         const ShapeList* before) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Vector) return;
    const ShapeList b = before ? *before : l->shapes;
    if (b == after) return;
    m_undo.push(new VectorStateCommand(this, layerId, b, after, text));
}

void Document::commitRasterPatch(const QUuid& layerId, const QRect& rect, const QImage& before,
                                 const QString& text) {
    Layer* l = findLayer(layerId);
    if (!l || l->raster.isNull() || rect.isEmpty()) return;
    const QRect r = rect.intersected(l->raster.rect());
    if (r.isEmpty()) return;
    // `before` is a full-layer snapshot or already the cropped patch.
    QImage b = before.size() == r.size() ? before : before.copy(r);
    m_undo.push(new RasterPatchCommand(this, layerId, r, b, l->raster.copy(r), text));
}

}  // namespace mynd
