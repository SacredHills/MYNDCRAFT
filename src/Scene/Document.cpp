#include "Scene/Document.h"
#include "Scene3D/Anim3D.h"

#include <QPainter>
#include <QSet>
#include <functional>

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
    m_ctx.clear();
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
void Document::emitScene3DChanged(const QUuid& id) { emit scene3DChanged(id); emitContentChanged({}); }
bool Document::editScene3D(const QUuid& layerId, const std::function<bool(Scene3D&)>& fn, const QString& text, const QString& mergeKey, bool autoKey) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Scene3D || !l->scene3d) return false;
    const Scene3D& base = *l->scene3d;
    const bool animated = hasSceneAnimation(base, l->tracks);
    if (!animated && !autoKey) {
        auto after = std::make_shared<Scene3D>(base);
        if (!fn(*after)) return false;
        m_undo.push(new Scene3DCommand(this, layerId, std::make_shared<Scene3D>(base), after, text, mergeKey));
        return true;
    }
    // The user edits what they see: the scene evaluated at the current frame.
    const int frame = currentFrame();
    const Scene3D seen = evaluateScene(base, l->tracks, frame);
    Scene3D edited = seen;
    if (!fn(edited)) return false;
    auto after = std::make_shared<Scene3D>(edited);
    PropertyTracks tracks = l->tracks;
    // Structural edits (nodes/materials added or removed) cannot be expressed as keys: apply them to the static scene.
    bool structural = seen.nodes.size() != edited.nodes.size() || seen.materials.size() != edited.materials.size();
    for (size_t i = 0; !structural && i < seen.nodes.size(); ++i) structural = seen.nodes[i].id != edited.nodes[i].id;
    for (size_t i = 0; !structural && i < seen.materials.size(); ++i) structural = seen.materials[i].id != edited.materials[i].id;
    if (structural) {
        after = std::make_shared<Scene3D>(base);
        if (!fn(*after)) return false;
        m_undo.push(new Scene3DCommand(this, layerId, std::make_shared<Scene3D>(base), after, text, mergeKey));
        return true;
    }
    const QSet<QString> driven = drivenProps(base, l->tracks);     // layer keyframes or placed clips
    for (const QString& id : animatableProps(edited)) {
        PropValue was, now, stat;
        if (!readSceneProp(seen, id, &was) || !readSceneProp(edited, id, &now) || !readSceneProp(base, id, &stat)) continue;
        const bool hasKeys = driven.contains(id);
        if (now != was) {
            if (hasKeys || autoKey) {
                const PropertyDef* d = scene3DPropertyDef(id);
                PropertyTrack& tr = tracks[id];                  // creates when missing
                if (!hasKeys) tr.base = stat;
                tr.setKey(*d, frame, now);
                writeSceneProp(*after, id, stat);                 // the scene keeps its static value
            }
            // otherwise the edited value stays in `after` and becomes the new static value
        } else if (hasKeys) {
            writeSceneProp(*after, id, stat);                     // an animated value that was not edited: back to the static one
        }
    }
    after->touch();
    if (after->sameAs(base) && tracks == l->tracks) return false;
    auto* cmd = new Scene3DCommand(this, layerId, std::make_shared<Scene3D>(base), after, text, mergeKey);
    cmd->setTracks(l->tracks, tracks);
    m_undo.push(cmd);
    return true;
}
bool Document::toggleScene3DAnimation(const QUuid& layerId, const QString& propId) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Scene3D || !l->scene3d) return false;
    const PropertyDef* d = scene3DPropertyDef(propId);
    PropValue stat;
    if (!d || !readSceneProp(*l->scene3d, propId, &stat)) return false;
    const int frame = currentFrame();
    auto after = std::make_shared<Scene3D>(*l->scene3d);
    PropertyTracks tracks = l->tracks;
    QString text;
    if (tracks.contains(propId) && tracks[propId].animated()) {
        writeSceneProp(*after, propId, tracks[propId].valueAt(*d, frame));
        tracks.remove(propId);
        text = tr("Stop Animating %1").arg(l->propertyLabel(propId));
    } else {
        PropertyTrack trk;
        PropValue shown = stat;                                    // a clip may be driving it: key what is on screen
        readSceneProp(*l->sceneAt(frame), propId, &shown);
        trk.base = stat;
        trk.setKey(*d, frame, shown);
        tracks.insert(propId, trk);
        text = tr("Animate %1").arg(l->propertyLabel(propId));
    }
    after->touch();
    auto* cmd = new Scene3DCommand(this, layerId, std::make_shared<Scene3D>(*l->scene3d), after, text, {});
    cmd->setTracks(l->tracks, tracks);
    m_undo.push(cmd);
    return true;
}

bool Document::toggleScene3DKey(const QUuid& layerId, const QString& propId) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Scene3D || !l->scene3d) return false;
    const PropertyDef* d = scene3DPropertyDef(propId);
    PropValue stat;
    if (!d || !readSceneProp(*l->scene3d, propId, &stat)) return false;
    const int frame = currentFrame();
    auto it = l->tracks.constFind(propId);
    const bool animated = it != l->tracks.constEnd() && it->animated();
    if (!animated) return toggleScene3DAnimation(layerId, propId);
    PropertyTracks tracks = l->tracks;
    PropertyTrack& trk = tracks[propId];
    if (trk.indexOfKey(frame) >= 0) {
        if (trk.keys.size() == 1) return toggleScene3DAnimation(layerId, propId);
        trk.removeKey(frame);
        m_undo.push(new TracksCommand(this, layerId, l->tracks, tracks, tr("Remove Keyframe"), {}));
    } else {
        trk.setKey(*d, frame, l->tracks[propId].valueAt(*d, frame));
        m_undo.push(new TracksCommand(this, layerId, l->tracks, tracks, tr("Add Keyframe"), {}));
    }
    return true;
}

QUuid Document::addScene3DLayer(const QString& name, QUuid parentId, int index) {
    auto l = std::make_shared<Layer>(LayerType::Scene3D, name.isEmpty() ? tr("3D Scene") : name);
    const QUuid id = l->id;
    auto* cmd = new AddLayerCommand(this, parentId, index, l);
    cmd->setText(tr("Add 3D Scene"));
    m_undo.push(cmd);
    return id;
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
    if (!m_ctx.isEmpty()) for (auto& d : m_symbols) if (d.id == m_ctx.last().symbolId) d.durationFrames = m_comp.durationFrames;   // inside a symbol the timeline length is the symbol's
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

namespace mynd {

void Document::setShapeTween(const QUuid& layerId, const ShapeList& shapes, const ShapeList& morph, bool editingEnd, const QString& text) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Vector) return;
    if (l->shapes == shapes && l->morphShapes == morph && l->editingEnd == editingEnd) return;
    m_undo.push(new ShapeTweenCommand(this, layerId, l->shapes, l->morphShapes, l->editingEnd, shapes, morph, editingEnd, text));
}

// ---------------- Symbols ----------------
const SymbolDef* Document::findSymbol(const QUuid& id) const {
    for (const auto& d : m_symbols) if (d.id == id) return &d;
    return nullptr;
}

QStringList Document::breadcrumb() const {
    QStringList b{tr("Scene")};
    for (const auto& c : m_ctx) if (const SymbolDef* d = findSymbol(c.symbolId)) b << d->name;
    return b;
}

CompositionSettings Document::sceneComp() const {
    CompositionSettings c = m_comp;
    if (!m_ctx.isEmpty()) {
        const EditCtx& e = m_ctx.first();
        c.durationFrames = e.savedDuration; c.workEnabled = e.savedWorkEnabled; c.workIn = e.savedWorkIn; c.workOut = e.savedWorkOut;
    }
    return c;
}

static bool containsInstanceOf(const Layer& g, const QUuid& inner) {
    for (const auto& c : g.children) {
        if (c->type == LayerType::Symbol && c->symbolId == inner) return true;
        if (containsInstanceOf(*c, inner)) return true;
    }
    return false;
}

bool Document::symbolUses(const QUuid& outer, const QUuid& inner) const {
    QSet<QUuid> seen;
    std::function<bool(const QUuid&)> walk = [&](const QUuid& id) {
        if (seen.contains(id)) return false;
        seen.insert(id);
        const SymbolDef* d = findSymbol(id);
        if (!d || !d->root) return false;
        if (containsInstanceOf(*d->root, inner)) return true;
        bool found = false;
        forEachLayer(d->root.get(), [&](Layer* l) {
            if (!found && l->type == LayerType::Symbol && walk(l->symbolId)) found = true;
        });
        return found;
    };
    return walk(outer);
}

bool Document::canInstantiate(const QUuid& symbolId) const {
    if (!findSymbol(symbolId)) return false;
    for (const auto& c : m_ctx) {                       // every symbol we are inside would contain it
        if (c.symbolId == symbolId || symbolUses(symbolId, c.symbolId)) return false;
    }
    return true;
}

int Document::symbolUseCount(const QUuid& id) const {
    int n = 0;
    auto count = [&](const Layer* root) {
        if (!root) return;
        forEachLayer(root, [&](Layer* l) { if (l->type == LayerType::Symbol && l->symbolId == id) ++n; });
    };
    count(sceneRoot());
    for (const auto& d : m_symbols) count(d.root.get());
    return n;
}

std::unique_ptr<Document> Document::symbolAsset(const QUuid& id) const {
    const SymbolDef* top = findSymbol(id);
    if (!top) return nullptr;
    QVector<QUuid> need;
    std::function<void(const QUuid&)> collect = [&](const QUuid& sid) {
        if (need.contains(sid)) return;
        const SymbolDef* d = findSymbol(sid);
        if (!d || !d->root) return;
        need.append(sid);
        forEachLayer(d->root.get(), [&](Layer* l) { if (l->type == LayerType::Symbol) collect(l->symbolId); });
    };
    collect(id);
    CompositionSettings cs = sceneComp();
    cs.durationFrames = top->durationFrames; cs.workEnabled = false; cs.guides.clear();
    auto doc = std::make_unique<Document>(cs);
    QVector<SymbolDef> defs;
    for (const QUuid& sid : need) {
        SymbolDef d = *findSymbol(sid);
        d.root = d.root->cloneExact();                 // the asset must not share layers with this project
        defs.append(d);
    }
    doc->rawSetSymbols(defs);
    auto inst = std::make_shared<Layer>(LayerType::Symbol, top->name);
    inst->symbolId = id;
    inst->parent = doc->root();
    doc->root()->children.push_back(inst);
    doc->markSaved();
    return doc;
}

static void fitLayerToCanvas(Layer& l, const QSize& from, const QSize& to) {
    if (from == to) return;
    const QPoint off((to.width() - from.width()) / 2, (to.height() - from.height()) / 2);
    auto recenter = [&](const QImage& img) {
        QImage out(to, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out); p.drawImage(off, img); p.end();
        return out;
    };
    if (!l.raster.isNull()) l.raster = recenter(l.raster);
    for (auto& [did, img] : l.drawingImages) img = recenter(img);
    for (auto& s : l.shapes) s.translate(QPointF(off));
    for (auto& s : l.morphShapes) s.translate(QPointF(off));
    l.textContent.pos += QPointF(off);
    for (auto& c : l.children) fitLayerToCanvas(*c, from, to);
}

static void renewIds(Layer& l) {
    l.id = QUuid::createUuid();
    for (auto& c : l.children) renewIds(*c);
}
static void remapInstances(Layer& l, const QHash<QUuid, QUuid>& map) {
    if (l.type == LayerType::Symbol && map.contains(l.symbolId)) l.symbolId = map[l.symbolId];
    for (auto& c : l.children) remapInstances(*c, map);
}

QVector<QUuid> Document::importSymbols(const Document& src) {
    QVector<QUuid> ids;
    if (src.symbols().isEmpty()) return ids;
    QHash<QUuid, QUuid> map;
    for (const SymbolDef& d : src.symbols()) map[d.id] = QUuid::createUuid();
    QVector<SymbolDef> after = m_symbols;
    for (const SymbolDef& d : src.symbols()) {
        SymbolDef n = d;
        n.id = map[d.id];
        n.root = d.root->cloneExact();
        renewIds(*n.root);
        remapInstances(*n.root, map);
        fixParents(n.root.get());
        fitLayerToCanvas(*n.root, src.canvasSize(), canvasSize());
        bool clash = true;
        for (int k = 2; clash; ++k) {                 // keep names unique in the library
            clash = false;
            for (const auto& e : after) if (e.name == n.name) { clash = true; break; }
            if (clash) n.name = QString("%1 %2").arg(d.name).arg(k);
        }
        after.append(n);
        ids.append(n.id);
    }
    m_undo.push(new SymbolLibraryCommand(this, m_symbols, after, tr("Import Symbols")));
    return ids;
}

void Document::rawSetSymbols(const QVector<SymbolDef>& s) {
    m_symbols = s;
    emit symbolsChanged();
    emitContentChanged({});
}

void Document::rawEnterSymbol(const QUuid& id) {
    const SymbolDef* d = findSymbol(id);
    if (!d || !d->root) return;
    EditCtx c;
    c.symbolId = id; c.savedRoot = m_root; c.savedDuration = m_comp.durationFrames;
    c.savedWorkEnabled = m_comp.workEnabled; c.savedWorkIn = m_comp.workIn; c.savedWorkOut = m_comp.workOut; c.savedFrame = m_frame;
    m_ctx.append(c);
    m_root = d->root;
    fixParents(m_root.get());
    m_comp.durationFrames = std::max(1, d->durationFrames);
    m_comp.workEnabled = false; m_comp.workIn = 0; m_comp.workOut = m_comp.durationFrames - 1;
    m_frame = 0;
    emit frameChanged(m_frame);
    emit timingChanged();
    emit contextChanged();
    emitTreeChanged();
    emitContentChanged({});
}

void Document::rawExitSymbol() {
    if (m_ctx.isEmpty()) return;
    const EditCtx c = m_ctx.takeLast();
    for (auto& d : m_symbols) if (d.id == c.symbolId) d.durationFrames = m_comp.durationFrames;   // the timeline length belongs to the symbol
    m_root = c.savedRoot;
    m_comp.durationFrames = c.savedDuration;
    m_comp.workEnabled = c.savedWorkEnabled; m_comp.workIn = c.savedWorkIn; m_comp.workOut = c.savedWorkOut;
    m_frame = std::min(c.savedFrame, m_comp.durationFrames - 1);
    emit frameChanged(m_frame);
    emit timingChanged();
    emit symbolsChanged();
    emit contextChanged();
    emitTreeChanged();
    emitContentChanged({});
}

QUuid Document::createSymbol(const QString& name, SymbolKind kind, int durationFrames) {
    SymbolDef d;
    d.id = QUuid::createUuid(); d.name = name; d.kind = kind; d.durationFrames = qBound(1, durationFrames, kMaxDurationFrames);
    d.root = std::make_shared<Layer>(LayerType::Group, "Symbol");
    QVector<SymbolDef> after = m_symbols; after.append(d);
    m_undo.push(new SymbolLibraryCommand(this, m_symbols, after, tr("New Symbol")));
    return d.id;
}

QUuid Document::convertToSymbol(const QUuid& layerId, const QString& name, SymbolKind kind) {
    Layer* l = findLayer(layerId);
    if (!l || !l->parent) return {};
    // A symbol cannot be made out of a layer that already holds an instance of a symbol we are inside of.
    for (const auto& c : m_ctx) {
        bool bad = false;
        auto check = [&](Layer* x) { if (x->type == LayerType::Symbol && (x->symbolId == c.symbolId || symbolUses(x->symbolId, c.symbolId))) bad = true; };
        check(l);
        forEachLayer(l, check);
        if (bad) return {};
    }
    SymbolDef d;
    d.id = QUuid::createUuid(); d.name = name; d.kind = kind; d.durationFrames = std::max(1, m_comp.durationFrames);
    d.root = std::make_shared<Layer>(LayerType::Group, "Symbol");
    auto inst = std::make_shared<Layer>(LayerType::Symbol, l->props.name);
    inst->symbolId = d.id;
    m_undo.push(new ConvertToSymbolCommand(this, layerId, d, inst));
    return d.id;
}

QUuid Document::addSymbolInstance(const QUuid& symbolId, const QUuid& parentId, int index) {
    const SymbolDef* d = findSymbol(symbolId);
    if (!d || !canInstantiate(symbolId)) return {};
    auto inst = std::make_shared<Layer>(LayerType::Symbol, d->name);
    inst->symbolId = symbolId;
    const QUuid id = inst->id;
    auto* cmd = new AddLayerCommand(this, parentId, index, inst);
    cmd->setText(tr("Add Symbol Instance"));
    m_undo.push(cmd);
    return id;
}

bool Document::deleteSymbol(const QUuid& id, QString* why) {
    if (!findSymbol(id)) return false;
    for (const auto& c : m_ctx) if (c.symbolId == id) { if (why) *why = tr("Exit the symbol before deleting it."); return false; }
    if (symbolUseCount(id) > 0) { if (why) *why = tr("The symbol still has instances; delete them first."); return false; }
    QVector<SymbolDef> after;
    for (const auto& d : m_symbols) if (d.id != id) after.append(d);
    m_undo.push(new SymbolLibraryCommand(this, m_symbols, after, tr("Delete Symbol")));
    return true;
}

void Document::renameSymbol(const QUuid& id, const QString& name) {
    if (name.trimmed().isEmpty()) return;
    QVector<SymbolDef> after = m_symbols;
    for (auto& d : after) if (d.id == id) d.name = name.trimmed();
    if (after == m_symbols) return;
    m_undo.push(new SymbolLibraryCommand(this, m_symbols, after, tr("Rename Symbol")));
}

void Document::setSymbolKind(const QUuid& id, SymbolKind kind) {
    QVector<SymbolDef> after = m_symbols;
    for (auto& d : after) if (d.id == id) d.kind = kind;
    if (after == m_symbols) return;
    m_undo.push(new SymbolLibraryCommand(this, m_symbols, after, tr("Change Symbol Type")));
}

void Document::setInstancePlayback(const QUuid& layerId, SymbolPlay play, int firstFrame) {
    Layer* l = findLayer(layerId);
    if (!l || l->type != LayerType::Symbol) return;
    firstFrame = std::max(0, firstFrame);
    if (l->symbolPlay == play && l->symbolFirstFrame == firstFrame) return;
    m_undo.push(new SymbolInstanceCommand(this, layerId, l->symbolPlay, l->symbolFirstFrame, play, firstFrame));
}

void Document::enterSymbol(const QUuid& id) {
    if (!findSymbol(id)) return;
    for (const auto& c : m_ctx) if (c.symbolId == id) return;
    auto* cmd = new SymbolContextCommand(this, id, true);
    m_undo.push(cmd);
}

void Document::exitSymbol() {
    if (m_ctx.isEmpty()) return;
    m_undo.push(new SymbolContextCommand(this, m_ctx.last().symbolId, false));
}

}  // namespace mynd
