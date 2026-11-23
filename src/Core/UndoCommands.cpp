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
RasterPatchCommand::RasterPatchCommand(Document* d, const QUuid& layer, const QRect& rect,
                                       const QImage& before, const QImage& after, const QString& text)
    : m_doc(d), m_layer(layer), m_rect(rect), m_before(before), m_after(after) {
    setText(text);
}
void RasterPatchCommand::apply(const QImage& img) {
    Layer* l = m_doc->findLayer(m_layer);
    if (!l || l->raster.isNull()) return;
    QPainter p(&l->raster);
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

}  // namespace mynd
