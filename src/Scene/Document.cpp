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
void Document::emitTracksChanged(const QUuid& id) { emit tracksChanged(id); emitContentChanged({}); }
void Document::editTracks(const QUuid& layerId, const PropertyTracks& after, const QString& text, const QString& mergeKey) {
    Layer* l = findLayer(layerId);
    if (!l) return;
    PropertyTracks clean = after;
    pruneDefaults(clean);
    if (l->tracks == clean) return;
    m_undo.push(new TracksCommand(this, layerId, l->tracks, clean, text, mergeKey));
}
void Document::emitAnimationChanged(const QUuid& id) { emit animationChanged(id); emitContentChanged({}); }

// ---------------- undoable edits ----------------
QUuid Document::addLayer(LayerType type, const QString& name, QUuid parentId, int index) {
    auto l = std::make_shared<Layer>(type, name);
    if (type == LayerType::Raster) l->raster = newRasterImage();
    const QUuid id = l->id;
    m_undo.push(new AddLayerCommand(this, parentId, index, l));
    return id;
}

QUuid Document::addTextLayer(const TextContent& content, const QString& name, QUuid parentId, int index) {
    auto l = std::make_shared<Layer>(LayerType::Text, name);
    l->textContent = content;
    const QUuid id = l->id;
    auto* cmd = new AddLayerCommand(this, parentId, index, l);
    cmd->setText(tr("Add Text"));
    m_undo.push(cmd);
    return id;
}

void Document::setTextContent(const QUuid& layerId, const TextContent& after, const QString& text, const QString& mergeKey) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Text || l->textContent == after) return;
    m_undo.push(new TextStateCommand(this, layerId, l->textContent, after, text, mergeKey));
}

QUuid Document::addImageLayer(const QImage& image, const QString& name, QUuid parentId, int index) {
    if (image.isNull()) return {};
    QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (src.width() > m_comp.width || src.height() > m_comp.height)
        src = src.scaled(canvasSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    auto l = std::make_shared<Layer>(LayerType::Raster, name);
    l->raster = newRasterImage();
    QPainter p(&l->raster);
    p.drawImage((m_comp.width - src.width()) / 2, (m_comp.height - src.height()) / 2, src);
    p.end();
    const QUuid id = l->id;
    auto* cmd = new AddLayerCommand(this, parentId, index, l);
    cmd->setText(tr("Import Image"));
    m_undo.push(cmd);
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

void Document::setCurrentFrame(int frame) {
    frame = qBound(0, frame, std::max(0, m_comp.durationFrames - 1));
    if (frame == m_frame) return;
    m_frame = frame;
    emit frameChanged(frame);
    emitContentChanged({});
}

int Document::rangeStart() const { return m_comp.workEnabled ? qBound(0, m_comp.workIn, m_comp.durationFrames - 1) : 0; }
int Document::rangeEnd() const {
    return m_comp.workEnabled ? qBound(rangeStart(), m_comp.workOut, m_comp.durationFrames - 1) : m_comp.durationFrames - 1;
}

static TimingSettings timingOf(const CompositionSettings& c) {
    TimingSettings t; t.fps = c.fps; t.durationFrames = c.durationFrames;
    t.workEnabled = c.workEnabled; t.workIn = c.workIn; t.workOut = c.workOut;
    return t;
}
void Document::applyTiming(const TimingSettings& t) {
    m_comp.fps = t.fps; m_comp.durationFrames = t.durationFrames;
    m_comp.workEnabled = t.workEnabled; m_comp.workIn = t.workIn; m_comp.workOut = t.workOut;
    if (m_frame >= m_comp.durationFrames) { m_frame = m_comp.durationFrames - 1; emit frameChanged(m_frame); }
    emit timingChanged();
    emitContentChanged({});
}
void Document::setDuration(int frames) {
    frames = qBound(1, frames, kMaxDurationFrames);
    TimingSettings a = timingOf(m_comp), b = a;
    b.durationFrames = frames;
    b.workOut = std::min(b.workOut, frames - 1);
    b.workIn = std::min(b.workIn, b.workOut);
    if (a == b) return;
    m_undo.push(new TimingCommand(this, a, b, tr("Set Duration")));
}
void Document::setFrameRate(FrameRate fps) {
    if (!fps.isValid() || fps == m_comp.fps) return;
    TimingSettings a = timingOf(m_comp), b = a;
    b.fps = fps;
    m_undo.push(new TimingCommand(this, a, b, tr("Set Frame Rate")));
}
void Document::setWorkArea(bool enabled, int in, int out) {
    out = qBound(0, out, m_comp.durationFrames - 1);
    in = qBound(0, in, out);
    TimingSettings a = timingOf(m_comp), b = a;
    b.workEnabled = enabled; b.workIn = in; b.workOut = out;
    if (a == b) return;
    m_undo.push(new TimingCommand(this, a, b, tr("Set Work Area")));
}

QUuid Document::addAnimationLayer(const QString& name, QUuid parentId, int index) {
    auto l = std::make_shared<Layer>(LayerType::Animation, name);
    const QUuid id = l->id;
    m_undo.push(new AddLayerCommand(this, parentId, index, l));
    return id;
}

void Document::editAnimation(const QUuid& layerId, const AnimationState& after, const QString& text) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Animation || l->anim == after) return;
    const bool grow = after.endFrame() > m_comp.durationFrames;
    if (grow) m_undo.beginMacro(text);
    if (grow) setDuration(after.endFrame());
    m_undo.push(new AnimationStateCommand(this, layerId, l->anim, after, text));
    if (grow) m_undo.endMacro();
}

QUuid Document::createDrawing(const QUuid& layerId, int frame, bool copyFromVisible, int sourceFrame, const QString& name) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Animation || frame < 0 || frame >= kMaxDurationFrames) return {};
    QImage img = newRasterImage();
    if (copyFromVisible) if (const QImage* src = l->imageAt(sourceFrame >= 0 ? sourceFrame : frame)) img = src->copy();
    const QUuid id = QUuid::createUuid();
    AnimationState after = l->anim;
    after.drawings.push_back({id, name.isEmpty() ? after.nextDrawingName() : name});
    after.setRange(frame, 1, id);
    l->drawingImages[id] = img;     // inert until an exposure refers to it; kept for redo/undo
    editAnimation(layerId, after, copyFromVisible ? tr("Duplicate Drawing") : tr("New Drawing"));
    return id;
}

void Document::commitRasterPatch(const QUuid& layerId, const QRect& rect, const QImage& before,
                                 const QString& text) {
    Layer* l = findLayer(layerId);
    if (!l || rect.isEmpty()) return;
    const QImage* cur = l->type == LayerType::Animation ? l->imageAt(m_frame) : &l->raster;
    if (!cur || cur->isNull()) return;
    const QRect r = rect.intersected(cur->rect());
    if (r.isEmpty()) return;
    // `before` is a full-layer snapshot or already the cropped patch.
    QImage b = before.size() == r.size() ? before : before.copy(r);
    QUuid drawing;
    const QImage* src = &l->raster;
    if (l->type == LayerType::Animation) { drawing = l->anim.drawingAt(m_frame); src = l->imageAt(m_frame); }
    if (!src) return;
    m_undo.push(new RasterPatchCommand(this, layerId, drawing, r, b, src->copy(r), text));
}

}  // namespace mynd
