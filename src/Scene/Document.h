#pragma once
#include <QColor>
#include <QObject>
#include <QUndoStack>
#include <QUuid>

#include "Core/FrameRate.h"
#include "Scene/Layer.h"

namespace mynd {

struct Guide {
    Qt::Orientation orientation = Qt::Horizontal;  // Horizontal = a horizontal line at y=pos
    double pos = 0;
    bool operator==(const Guide& o) const { return orientation == o.orientation && pos == o.pos; }
};

struct CompositionSettings {
    int width = 1920;
    int height = 1080;
    FrameRate fps{24, 1};
    QColor background = Qt::white;  // alpha 0 = transparent
    double gridSize = 50;
    QVector<Guide> guides;
    // Timeline
    int durationFrames = 48;
    bool workEnabled = false;   // loop/play only [workIn, workOut] when enabled
    int workIn = 0;
    int workOut = 47;           // inclusive
};

constexpr int kMaxDurationFrames = 1'000'000;

constexpr int kMaxCanvasDim = 16384;

// The persistent project model: one composition with a layer tree. All edits that should be
// undoable go through the edit* methods, which push commands onto `undoStack()`.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(const CompositionSettings& s = {}, QObject* parent = nullptr);
    ~Document() override;

    QUuid docId() const { return m_docId; }
    void setDocId(const QUuid& id) { m_docId = id; }
    const CompositionSettings& comp() const { return m_comp; }
    CompositionSettings& compMutable() { return m_comp; }
    QSize canvasSize() const { return {m_comp.width, m_comp.height}; }
    QRect canvasRect() const { return {0, 0, m_comp.width, m_comp.height}; }

    Layer* root() const { return m_root.get(); }
    LayerPtr rootPtr() const { return m_root; }
    void setRoot(LayerPtr r);
    Layer* findLayer(const QUuid& id) const;
    LayerPtr findLayerPtr(const QUuid& id) const;
    // Index of layer within its parent's children, -1 if not found.
    int indexInParent(const Layer* l) const;
    static void forEachLayer(const Layer* g, const std::function<void(Layer*)>& fn);
    int layerCount() const;
    static void fixParents(Layer* g);
    // New blank raster canvas for this document's size.
    QImage newRasterImage() const;

    QUndoStack* undoStack() { return &m_undo; }
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& p) { m_filePath = p; }
    bool isModified() const { return !m_undo.isClean() || m_extraDirty; }
    void markSaved();
    void markDirty();

    // ---- Undoable edits ----
    // Add a layer inside `parentId` (null = root) at `index` (-1 = top). Returns new layer id.
    QUuid addLayer(LayerType type, const QString& name, QUuid parentId = {}, int index = -1);
    void removeLayer(const QUuid& id);
    QUuid duplicateLayer(const QUuid& id);
    void moveLayer(const QUuid& id, const QUuid& newParent, int newIndex);
    QUuid groupLayer(const QUuid& id);   // wraps layer in a new group
    void ungroupLayer(const QUuid& groupId);
    void setLayerProps(const QUuid& id, const LayerProps& p, const QString& mergeKey = {});
    void setShapes(const QUuid& layerId, const ShapeList& after, const QString& text,
                   const ShapeList* before = nullptr);
    // ---- Timeline / traditional animation ----
    int currentFrame() const { return m_frame; }
    void setCurrentFrame(int frame);                 // clamps to [0, duration-1]; not an undoable edit
    int durationFrames() const { return m_comp.durationFrames; }
    void setDuration(int frames);
    void setFrameRate(FrameRate fps);
    void setWorkArea(bool enabled, int in, int out);
    // First/last frame of the playback range (work area when enabled, else the whole duration).
    int rangeStart() const;
    int rangeEnd() const;
    QUuid addAnimationLayer(const QString& name, QUuid parentId = {}, int index = -1);
    // Replace the timing data of an animation layer; the duration grows if exposures reach past it.
    void editAnimation(const QUuid& layerId, const AnimationState& after, const QString& text);
    // Create a drawing exposed for one frame at `frame` (replacing whatever was exposed there).
    // copyFromVisible duplicates the pixels of the drawing shown at `sourceFrame` (default: `frame` itself).
    QUuid createDrawing(const QUuid& layerId, int frame, bool copyFromVisible, int sourceFrame = -1,
                       const QString& name = {});
    // The bitmap that tools should edit for this layer at the current frame (null = nothing to edit).
    QImage* editImage(Layer* l) { return l ? l->imageForEdit(m_frame) : nullptr; }
    void applyTiming(const struct TimingSettings& t);   // used by TimingCommand
    void emitAnimationChanged(const QUuid& layerId);

    QUuid addTextLayer(const TextContent& content, const QString& name, QUuid parentId = {}, int index = -1);
    // Replaces the content of a text layer. Edits with the same non-empty mergeKey collapse into one undo step.
    void setTextContent(const QUuid& layerId, const TextContent& after, const QString& text,
                        const QString& mergeKey = {});
    // Adds a raster layer holding `image` (scaled down to fit the canvas if larger, centered).
    QUuid addImageLayer(const QImage& image, const QString& name, QUuid parentId = {}, int index = -1);
    // The pixels of `layer` inside `rect` were already modified in place; record for undo.
    void commitRasterPatch(const QUuid& layerId, const QRect& rect, const QImage& before,
                           const QString& text);

    // ---- Low-level mutators used by commands (no undo) ----
    void rawInsert(const QUuid& parentId, int index, LayerPtr layer);
    // notify=false is for the first half of a move: observers must not see the layer as deleted.
    LayerPtr rawRemove(const QUuid& id, QUuid* parentId = nullptr, int* index = nullptr, bool notify = true);

    // Notifications (also used by commands).
    void emitTreeChanged();
    void emitContentChanged(const QRect& r);  // empty rect = everything
    void emitPropsChanged(const QUuid& id);

signals:
    void layerTreeChanged();
    void layerPropsChanged(const QUuid& id);
    void contentChanged(const QRect& docRect);
    void modifiedChanged(bool modified);
    void frameChanged(int frame);
    void animationChanged(const QUuid& layerId);
    void timingChanged();

private:
    QUuid m_docId = QUuid::createUuid();
    CompositionSettings m_comp;
    LayerPtr m_root;
    QUndoStack m_undo;
    QString m_filePath;
    bool m_extraDirty = false;
    int m_frame = 0;
};

}  // namespace mynd
