#include "Core/UndoCommands.h"

#include <QPainter>

namespace mynd {

// ---------------- Add ----------------
AddLayerCommand::AddLayerCommand(Document* d, const QUuid& parentId, int index, LayerPtr layer)
    : m_doc(d), m_parent(parentId), m_index(index), m_layer(std::move(layer)) {
    setText(QObject::tr("Add Layer"));
}
void AddLayerCommand::redo() { m_doc->rawInsert(m_parent, m_index, m_layer); }
void AddLayerCommand::undo() { m_doc->rawRemove(m_layer->id); }

// ---------------- Remove ----------------
RemoveLayerCommand::RemoveLayerCommand(Document* d, const QUuid& id) : m_doc(d), m_id(id) {
    setText(QObject::tr("Delete Layer"));
}
void RemoveLayerCommand::redo() {
    m_layer = m_doc->rawRemove(m_id, &m_parent, &m_index);
}
void RemoveLayerCommand::undo() {
    if (m_layer) m_doc->rawInsert(m_parent, m_index, m_layer);
}

// ---------------- Move ----------------
MoveLayerCommand::MoveLayerCommand(Document* d, const QUuid& id, const QUuid& np, int ni)
    : m_doc(d), m_id(id), m_newParent(np), m_newIndex(ni) {
    setText(QObject::tr("Move Layer"));
    if (Layer* l = d->findLayer(id)) {
        m_oldParent = l->parent && l->parent != d->root() ? l->parent->id : QUuid();
        m_oldIndex = d->indexInParent(l);
    }
}
void MoveLayerCommand::redo() {
    LayerPtr l = m_doc->rawRemove(m_id, nullptr, nullptr, false);
    if (l) m_doc->rawInsert(m_newParent, m_newIndex, l);
}
void MoveLayerCommand::undo() {
    LayerPtr l = m_doc->rawRemove(m_id, nullptr, nullptr, false);
    if (l) m_doc->rawInsert(m_oldParent, m_oldIndex, l);
}

// ---------------- Props ----------------
LayerPropsCommand::LayerPropsCommand(Document* d, const QUuid& id, const LayerProps& b,
                                     const LayerProps& a, const QString& key)
    : m_doc(d), m_layer(id), m_before(b), m_after(a), m_mergeKey(key) {
    setText(QObject::tr("Change Layer Properties"));
    if (b.name != a.name) setText(QObject::tr("Rename Layer"));
    else if (b.visible != a.visible) setText(QObject::tr("Toggle Layer Visibility"));
    else if (b.locked != a.locked) setText(QObject::tr("Toggle Layer Lock"));
    else if (b.opacity != a.opacity) setText(QObject::tr("Layer Opacity"));
    else if (b.blend != a.blend) setText(QObject::tr("Layer Blend Mode"));
}
void LayerPropsCommand::redo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->props = m_after; m_doc->emitPropsChanged(m_layer); m_doc->emitContentChanged({}); }
}
void LayerPropsCommand::undo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->props = m_before; m_doc->emitPropsChanged(m_layer); m_doc->emitContentChanged({}); }
}
bool LayerPropsCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const LayerPropsCommand*>(other);
    if (!o || o->m_layer != m_layer || o->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty()) return false;
    m_after = o->m_after;
    return true;
}

// ---------------- Raster patch ----------------
RasterPatchCommand::RasterPatchCommand(Document* d, const QUuid& layer, const QUuid& drawing, const QRect& rect,
                                       const QImage& before, const QImage& after, const QString& text)
    : m_doc(d), m_layer(layer), m_drawing(drawing), m_rect(rect), m_before(before), m_after(after) {
    setText(text);
}
void RasterPatchCommand::apply(const QImage& img) {
    Layer* l = m_doc->findLayer(m_layer);
    if (!l) return;
    QImage* target = l->type == LayerType::Animation ? l->drawingImage(m_drawing) : &l->raster;
    if (!target || target->isNull()) return;
    QPainter p(target);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(m_rect.topLeft(), img);
    p.end();
    m_doc->emitContentChanged(m_rect);
}
void RasterPatchCommand::redo() {
    if (m_first) { m_first = false; return; }  // edit was applied live while drawing
    apply(m_after);
}
void RasterPatchCommand::undo() { apply(m_before); }

// ---------------- Animation ----------------
AnimationStateCommand::AnimationStateCommand(Document* d, const QUuid& layer, const AnimationState& b,
                                             const AnimationState& a, const QString& text)
    : m_doc(d), m_layer(layer), m_before(b), m_after(a) { setText(text); }
void AnimationStateCommand::redo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->anim = m_after; m_doc->emitAnimationChanged(m_layer); }
}
void AnimationStateCommand::undo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->anim = m_before; m_doc->emitAnimationChanged(m_layer); }
}
TracksCommand::TracksCommand(Document* d, const QUuid& layer, const PropertyTracks& b, const PropertyTracks& a,
                             const QString& text, const QString& mergeKey)
    : m_doc(d), m_layer(layer), m_before(b), m_after(a), m_key(mergeKey) { setText(text); }
void TracksCommand::redo() { if (Layer* l = m_doc->findLayer(m_layer)) { l->tracks = m_after; m_doc->emitTracksChanged(m_layer); } }
void TracksCommand::undo() { if (Layer* l = m_doc->findLayer(m_layer)) { l->tracks = m_before; m_doc->emitTracksChanged(m_layer); } }
bool TracksCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const TracksCommand*>(other);
    if (!o || m_key.isEmpty() || o->m_key != m_key || o->m_layer != m_layer) return false;
    m_after = o->m_after;
    return true;
}
Scene3DCommand::Scene3DCommand(Document* d, const QUuid& layer, std::shared_ptr<const Scene3D> b, std::shared_ptr<const Scene3D> a,
                               const QString& text, const QString& mergeKey)
    : m_doc(d), m_layer(layer), m_before(std::move(b)), m_after(std::move(a)), m_key(mergeKey) { setText(text); }
void Scene3DCommand::apply(const Scene3D& s, const PropertyTracks* t) {
    Layer* l = m_doc->findLayer(m_layer);
    if (!l || !l->scene3d) return;
    *l->scene3d = s;
    l->scene3d->touch();
    if (t) { l->tracks = *t; m_doc->emitTracksChanged(m_layer); }
    m_doc->emitScene3DChanged(m_layer);
}
void Scene3DCommand::redo() { apply(*m_after, m_hasTracks ? &m_ta : nullptr); }
void Scene3DCommand::undo() { apply(*m_before, m_hasTracks ? &m_tb : nullptr); }
bool Scene3DCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const Scene3DCommand*>(other);
    if (!o || m_key.isEmpty() || o->m_key != m_key || o->m_layer != m_layer) return false;
    m_after = o->m_after;
    if (o->m_hasTracks) { if (!m_hasTracks) { m_tb = o->m_tb; m_hasTracks = true; } m_ta = o->m_ta; }
    return true;
}
TimingCommand::TimingCommand(Document* d, const TimingSettings& b, const TimingSettings& a, const QString& text)
    : m_doc(d), m_before(b), m_after(a) { setText(text); }
void TimingCommand::redo() { m_doc->applyTiming(m_after); }
void TimingCommand::undo() { m_doc->applyTiming(m_before); }

// ---------------- Composition background ----------------
LayerFxCommand::LayerFxCommand(Document* d, const QUuid& layer, const LayerFx& b, const LayerFx& a, const QString& text, const QString& key)
    : m_doc(d), m_layer(layer), m_before(b), m_after(a), m_key(key) { setText(text); }
void LayerFxCommand::redo() { m_doc->applyLayerFx(m_layer, m_after); }
void LayerFxCommand::undo() { m_doc->applyLayerFx(m_layer, m_before); }
bool LayerFxCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const LayerFxCommand*>(other);
    if (!o || m_key.isEmpty() || o->m_key != m_key || o->m_layer != m_layer) return false;
    m_after = o->m_after;
    return true;
}

BackgroundCommand::BackgroundCommand(Document* d, const QColor& b, const QColor& a, const QString& text) : m_doc(d), m_before(b), m_after(a) { setText(text); }
void BackgroundCommand::redo() { m_doc->applyBackground(m_after); }
void BackgroundCommand::undo() { m_doc->applyBackground(m_before); }

// ---------------- Vector state ----------------
VectorStateCommand::VectorStateCommand(Document* d, const QUuid& layer, const ShapeList& b,
                                       const ShapeList& a, const QString& text)
    : m_doc(d), m_layer(layer), m_before(b), m_after(a) {
    setText(text);
}
void VectorStateCommand::redo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->shapes = m_after; m_doc->emitContentChanged({}); }
}
void VectorStateCommand::undo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->shapes = m_before; m_doc->emitContentChanged({}); }
}

// ---------------- Text ----------------
TextStateCommand::TextStateCommand(Document* d, const QUuid& layer, const TextContent& b, const TextContent& a,
                                   const QString& text, const QString& key)
    : m_doc(d), m_layer(layer), m_before(b), m_after(a), m_mergeKey(key) { setText(text); }
void TextStateCommand::redo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->textContent = m_after; m_doc->emitContentChanged({}); }
}
void TextStateCommand::undo() {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->textContent = m_before; m_doc->emitContentChanged({}); }
}
bool TextStateCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const TextStateCommand*>(other);
    if (!o || o->m_layer != m_layer || o->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty()) return false;
    m_after = o->m_after;
    return true;
}

ShapeTweenCommand::ShapeTweenCommand(Document* d, const QUuid& layer, const Layer::PoseLayout& b, const Layer::PoseLayout& a, const QString& text)
    : m_doc(d), m_layer(layer), m_b(b), m_a(a) { setText(text); }
void ShapeTweenCommand::apply(const Layer::PoseLayout& s) {
    if (Layer* l = m_doc->findLayer(m_layer)) {
        l->shapes = s.shapes; l->morphShapes = s.morph; l->morphExtra = s.extra; l->editingPose = s.editing;
        m_doc->emitPropsChanged(m_layer); m_doc->emitContentChanged({});
    }
}
void ShapeTweenCommand::redo() { apply(m_a); }
void ShapeTweenCommand::undo() { apply(m_b); }

// ---------------- Symbols ----------------
SymbolLibraryCommand::SymbolLibraryCommand(Document* d, const QVector<SymbolDef>& b, const QVector<SymbolDef>& a, const QString& text)
    : m_doc(d), m_before(b), m_after(a) { setText(text); }
void SymbolLibraryCommand::redo() { m_doc->rawSetSymbols(m_after); }
void SymbolLibraryCommand::undo() { m_doc->rawSetSymbols(m_before); }

ConvertToSymbolCommand::ConvertToSymbolCommand(Document* d, const QUuid& layerId, const SymbolDef& def, LayerPtr instance)
    : m_doc(d), m_layerId(layerId), m_def(def), m_instance(std::move(instance)) {
    setText(QObject::tr("Convert to Symbol"));
}
void ConvertToSymbolCommand::redo() {
    m_before = m_doc->symbols();
    m_layer = m_doc->rawRemove(m_layerId, &m_parent, &m_index, false);
    if (!m_layer) return;
    m_def.root->children.clear();
    m_def.root->children.push_back(m_layer);
    m_layer->parent = m_def.root.get();
    QVector<SymbolDef> after = m_before; after.append(m_def);
    m_doc->rawSetSymbols(after);
    m_doc->rawInsert(m_parent, m_index, m_instance);
}
void ConvertToSymbolCommand::undo() {
    if (!m_layer) return;
    m_doc->rawRemove(m_instance->id, nullptr, nullptr, false);
    m_def.root->children.clear();
    m_layer->parent = nullptr;
    m_doc->rawSetSymbols(m_before);
    m_doc->rawInsert(m_parent, m_index, m_layer);
}

SymbolContextCommand::SymbolContextCommand(Document* d, const QUuid& symbolId, bool enter)
    : m_doc(d), m_symbol(symbolId), m_enter(enter) {
    setText(enter ? QObject::tr("Edit Symbol") : QObject::tr("Exit Symbol"));
}
void SymbolContextCommand::redo() { if (m_enter) m_doc->rawEnterSymbol(m_symbol); else m_doc->rawExitSymbol(); }
void SymbolContextCommand::undo() { if (m_enter) m_doc->rawExitSymbol(); else m_doc->rawEnterSymbol(m_symbol); }

SymbolInstanceCommand::SymbolInstanceCommand(Document* d, const QUuid& layer, SymbolPlay bp, int bf, SymbolPlay ap, int af)
    : m_doc(d), m_layer(layer), m_bp(bp), m_ap(ap), m_bf(bf), m_af(af) { setText(QObject::tr("Symbol Playback")); }
void SymbolInstanceCommand::apply(SymbolPlay p, int f) {
    if (Layer* l = m_doc->findLayer(m_layer)) { l->symbolPlay = p; l->symbolFirstFrame = f; m_doc->emitPropsChanged(m_layer); m_doc->emitContentChanged({}); }
}
void SymbolInstanceCommand::redo() { apply(m_ap, m_af); }
void SymbolInstanceCommand::undo() { apply(m_bp, m_bf); }

}  // namespace mynd
