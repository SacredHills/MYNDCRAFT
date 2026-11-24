#pragma once
#include <QImage>
#include <QUndoCommand>

#include "Scene/Document.h"

namespace mynd {

class AddLayerCommand : public QUndoCommand {
public:
    AddLayerCommand(Document* d, const QUuid& parentId, int index, LayerPtr layer);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_parent; int m_index; LayerPtr m_layer;
};

class RemoveLayerCommand : public QUndoCommand {
public:
    RemoveLayerCommand(Document* d, const QUuid& id);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_id; QUuid m_parent; int m_index = -1; LayerPtr m_layer;
};

class MoveLayerCommand : public QUndoCommand {
public:
    MoveLayerCommand(Document* d, const QUuid& id, const QUuid& newParent, int newIndex);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_id; QUuid m_oldParent, m_newParent; int m_oldIndex = -1, m_newIndex;
};

class LayerPropsCommand : public QUndoCommand {
public:
    LayerPropsCommand(Document* d, const QUuid& id, const LayerProps& before, const LayerProps& after,
                      const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1001; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    Document* m_doc; QUuid m_layer; LayerProps m_before, m_after; QString m_mergeKey;
};

class RasterPatchCommand : public QUndoCommand {
public:
    // `drawing` is the drawing being edited for animation layers (null for raster layers).
    RasterPatchCommand(Document* d, const QUuid& layer, const QUuid& drawing, const QRect& rect,
                       const QImage& before, const QImage& after, const QString& text);
    void undo() override;
    void redo() override;
private:
    void apply(const QImage& img);
    Document* m_doc; QUuid m_layer, m_drawing; QRect m_rect; QImage m_before, m_after; bool m_first = true;
};

// Timing data (and drawing list) of an animation layer.
class AnimationStateCommand : public QUndoCommand {
public:
    AnimationStateCommand(Document* d, const QUuid& layer, const AnimationState& before,
                          const AnimationState& after, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_layer; AnimationState m_before, m_after;
};

// Keyframes and static values of a layer's animatable properties.
class TracksCommand : public QUndoCommand {
public:
    TracksCommand(Document* d, const QUuid& layer, const PropertyTracks& before, const PropertyTracks& after,
                  const QString& text, const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_key.isEmpty() ? -1 : 0x7F3A; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    Document* m_doc; QUuid m_layer; PropertyTracks m_before, m_after; QString m_key;
};

class Scene3DCommand : public QUndoCommand {
public:
    Scene3DCommand(Document* d, const QUuid& layer, std::shared_ptr<const Scene3D> before, std::shared_ptr<const Scene3D> after,
                   const QString& text, const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_key.isEmpty() ? -1 : 0x7F3B; }
    bool mergeWith(const QUndoCommand* other) override;
    // Also swaps the layer's property tracks (edits that create or change 3D keyframes).
    void setTracks(const PropertyTracks& before, const PropertyTracks& after) { m_tb = before; m_ta = after; m_hasTracks = true; }
private:
    void apply(const Scene3D& s, const PropertyTracks* t);
    Document* m_doc; QUuid m_layer; std::shared_ptr<const Scene3D> m_before, m_after; QString m_key;
    PropertyTracks m_tb, m_ta; bool m_hasTracks = false;
};

// Frame rate, duration and work area of the composition.
struct TimingSettings {
    FrameRate fps{24, 1};
    int durationFrames = 48;
    bool workEnabled = false;
    int workIn = 0, workOut = 47;
    bool operator==(const TimingSettings& o) const {
        return fps == o.fps && durationFrames == o.durationFrames && workEnabled == o.workEnabled &&
               workIn == o.workIn && workOut == o.workOut;
    }
};
class TimingCommand : public QUndoCommand {
public:
    TimingCommand(Document* d, const TimingSettings& before, const TimingSettings& after, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; TimingSettings m_before, m_after;
};

class VectorStateCommand : public QUndoCommand {
public:
    VectorStateCommand(Document* d, const QUuid& layer, const ShapeList& before,
                       const ShapeList& after, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_layer; ShapeList m_before, m_after;
};

class TextStateCommand : public QUndoCommand {
public:
    TextStateCommand(Document* d, const QUuid& layer, const TextContent& before, const TextContent& after,
                     const QString& text, const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1002; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    Document* m_doc; QUuid m_layer; TextContent m_before, m_after; QString m_mergeKey;
};

// Start/end shapes and the edit-end flag of a vector layer, changed together.
class ShapeTweenCommand : public QUndoCommand {
public:
    ShapeTweenCommand(Document* d, const QUuid& layer, const ShapeList& bs, const ShapeList& bm, bool be,
                      const ShapeList& as, const ShapeList& am, bool ae, const QString& text);
    void undo() override;
    void redo() override;
private:
    void apply(const ShapeList& s, const ShapeList& m, bool e);
    Document* m_doc; QUuid m_layer; ShapeList m_bs, m_bm, m_as, m_am; bool m_be, m_ae;
};

// ---- Symbols ----
class SymbolLibraryCommand : public QUndoCommand {
public:
    SymbolLibraryCommand(Document* d, const QVector<SymbolDef>& before, const QVector<SymbolDef>& after, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QVector<SymbolDef> m_before, m_after;
};

// Moves a layer into a brand-new symbol and puts an instance of it where the layer was.
class ConvertToSymbolCommand : public QUndoCommand {
public:
    ConvertToSymbolCommand(Document* d, const QUuid& layerId, const SymbolDef& def, LayerPtr instance);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_layerId; SymbolDef m_def; LayerPtr m_instance, m_layer;
    QUuid m_parent; int m_index = -1; QVector<SymbolDef> m_before;
};

// Entering / leaving a symbol is part of the history so that undo always runs in the context its step was made in.
class SymbolContextCommand : public QUndoCommand {
public:
    SymbolContextCommand(Document* d, const QUuid& symbolId, bool enter);
    void undo() override;
    void redo() override;
private:
    Document* m_doc; QUuid m_symbol; bool m_enter;
};

class SymbolInstanceCommand : public QUndoCommand {
public:
    SymbolInstanceCommand(Document* d, const QUuid& layer, SymbolPlay bp, int bf, SymbolPlay ap, int af);
    void undo() override;
    void redo() override;
private:
    void apply(SymbolPlay p, int f);
    Document* m_doc; QUuid m_layer; SymbolPlay m_bp, m_ap; int m_bf, m_af;
};

}  // namespace mynd
