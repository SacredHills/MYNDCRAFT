#pragma once
#include <QColor>
#include <QObject>
#include <QUndoStack>
#include <QUuid>

#include "Core/FrameRate.h"
#include <QStringList>
#include <QVector>

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
class Document : public QObject, public SymbolResolver {
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
    // Keyframes / static values of a layer's animatable properties. `mergeKey` merges consecutive edits (drags).
    void editTracks(const QUuid& layerId, const PropertyTracks& after, const QString& text, const QString& mergeKey = {});
    void emitTracksChanged(const QUuid& layerId);
    void emitScene3DChanged(const QUuid& layerId);
    // 3D scenes: `fn` edits a copy of the layer's scene and returns whether it changed anything; the edit is one undo step
    // (steps with the same non-empty mergeKey on the same layer merge, for drags and spin boxes).
    // Undoable edit of a 3D scene layer. `fn` edits the scene as the user sees it at the current frame; when the layer has 3D
    // keyframes (or autoKey is on) changes to animatable properties become keyframes at the current frame instead of changing the static
    // values (see Anim3D.h); everything else is applied to the scene itself.
    // Stopwatch of a 3D property (Anim3D.h id): starts animating it with a key at the current frame holding its current value, or stops
    // animating and freezes the value shown at the current frame.
    bool toggleScene3DAnimation(const QUuid& layerId, const QString& propId);
    // Adds a key at the current frame holding the value shown there, or removes the key if there is one (starts animating if needed).
    bool toggleScene3DKey(const QUuid& layerId, const QString& propId);
    bool editScene3D(const QUuid& layerId, const std::function<bool(Scene3D&)>& fn, const QString& text, const QString& mergeKey = {}, bool autoKey = false);
    QUuid addScene3DLayer(const QString& name = {}, QUuid parentId = {}, int index = -1);

    // Shape tween state of a vector layer (end shapes + whether they are being edited). Undoable.
    void setShapeTween(const QUuid& layerId, const ShapeList& shapes, const ShapeList& morph, bool editingEnd, const QString& text);

    // ---- Symbols (reusable artwork with its own timeline) ----
    const QVector<SymbolDef>& symbols() const { return m_symbols; }
    const SymbolDef* findSymbol(const QUuid& id) const override;
    // While editing a symbol, root()/comp().durationFrames/the current frame all refer to the symbol; sceneRoot() and
    // sceneComp() always describe the main scene (used when saving).
    bool editingSymbol() const { return !m_ctx.isEmpty(); }
    QUuid editingSymbolId() const { return m_ctx.isEmpty() ? QUuid() : m_ctx.last().symbolId; }
    QStringList breadcrumb() const;                      // "Scene", then the names of the symbols being edited
    Layer* sceneRoot() const { return m_ctx.isEmpty() ? m_root.get() : m_ctx.first().savedRoot.get(); }
    CompositionSettings sceneComp() const;
    bool symbolUses(const QUuid& outer, const QUuid& inner) const;   // does `outer` (transitively) contain an instance of `inner`?
    bool canInstantiate(const QUuid& symbolId) const;    // false if an instance here would make a symbol contain itself
    int symbolUseCount(const QUuid& id) const;           // instances in the scene and in every symbol
    QUuid createSymbol(const QString& name, SymbolKind kind, int durationFrames);
    // Moves a layer (or group) into a new symbol and puts an instance in its place. Returns the symbol id.
    QUuid convertToSymbol(const QUuid& layerId, const QString& name, SymbolKind kind);
    QUuid addSymbolInstance(const QUuid& symbolId, const QUuid& parentId = {}, int index = -1);   // returns the layer id, or null
    bool deleteSymbol(const QUuid& id, QString* why = nullptr);       // refused while instances exist
    void renameSymbol(const QUuid& id, const QString& name);
    void setSymbolKind(const QUuid& id, SymbolKind kind);
    void setInstancePlayback(const QUuid& layerId, SymbolPlay play, int firstFrame);
    void enterSymbol(const QUuid& id);                   // undoable
    void exitSymbol();                                   // undoable
    // Reusable assets: a symbol (with the symbols it uses) as a stand-alone document, and the reverse. An imported
    // symbol gets new ids everywhere, so importing the same asset twice gives two independent copies.
    std::unique_ptr<Document> symbolAsset(const QUuid& id) const;
    QVector<QUuid> importSymbols(const Document& source);      // undoable; returns the new symbol ids
    void rawSetSymbols(const QVector<SymbolDef>& s);
    void rawEnterSymbol(const QUuid& id);
    void rawExitSymbol();

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
    void tracksChanged(const QUuid& layerId);
    void scene3DChanged(const QUuid& layerId);
    void timingChanged();
    void symbolsChanged();
    void contextChanged();                // entered or left a symbol

private:
    QUuid m_docId = QUuid::createUuid();
    CompositionSettings m_comp;
    LayerPtr m_root;
    QUndoStack m_undo;
    QString m_filePath;
    bool m_extraDirty = false;
    int m_frame = 0;
    QVector<SymbolDef> m_symbols;
    struct EditCtx {
        QUuid symbolId;
        LayerPtr savedRoot;
        int savedDuration = 0, savedWorkIn = 0, savedWorkOut = 0, savedFrame = 0;
        bool savedWorkEnabled = false;
    };
    QVector<EditCtx> m_ctx;
};

}  // namespace mynd
