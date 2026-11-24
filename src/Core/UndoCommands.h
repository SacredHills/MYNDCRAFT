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

}  // namespace mynd
