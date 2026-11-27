#pragma once
#include <QColor>
#include <QObject>
#include <QSet>

#include <functional>

#include "Drawing/BrushEngine.h"
#include "Graphics/Compositor.h"
#include "Scene/Playback.h"
#include "Scene/Document.h"
#include "Scene/TextContent.h"

namespace mynd {

// Frames copied from an animation layer: exposures (relative to the copied range, gaps preserved) plus
// the pixels of every drawing they use, so a paste works on any animation layer.
struct FrameClip {
    int length = 0;
    QVector<Exposure> exposures;
    QVector<DrawingMeta> drawings;
    QHash<QUuid, QImage> images;
    QUuid sourceLayer;
    bool isEmpty() const { return length <= 0; }
};

// One selected keyframe of one property of one layer.
struct KeyRef {
    QUuid layer;
    QString prop;
    int frame = 0;
    bool operator==(const KeyRef& o) const { return layer == o.layer && prop == o.prop && frame == o.frame; }
};
// Copied keyframes, with frames relative to the earliest one.
struct KeyClip {
    QMap<QString, QVector<Keyframe>> keys;
    bool isEmpty() const { return keys.isEmpty(); }
};

enum class ToolId {
    Select, DirectSelect, Brush, Pencil, Eraser, Pen, Line, Rectangle, Ellipse, Polygon,
    Bucket, Gradient, Eyedropper, Hand, Zoom, Smudge, FreeTransform, Text, LayerTransform
};
QString toolName(ToolId t);

struct VectorStyle {
    bool fillEnabled = true;
    QColor fill = QColor("#4a90d9");
    bool strokeEnabled = true;
    QColor stroke = QColor("#1b1b1b");
    double strokeWidth = 3.0;
};

// Transient editing state (not saved in the project): current tool, colors, brushes, selection,
// active layer, view toggles. Tools and panels talk to each other through this object.
class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject* parent = nullptr);

    Document* doc() const { return m_doc; }
    void setDocument(Document* d);

    // Active layer / selection
    QUuid activeLayerId() const { return m_activeLayer; }
    Layer* activeLayer() const { return m_doc ? m_doc->findLayer(m_activeLayer) : nullptr; }
    void setActiveLayer(const QUuid& id);
    // ---- 3D ----
    // The 3D scene layer the 3D panels edit: the active layer when it is a 3D scene, otherwise null.
    Layer* active3DLayer() const { Layer* l = activeLayer(); return l && l->type == LayerType::Scene3D && l->scene3d ? l : nullptr; }
    QUuid selectedNode() const { return m_node; }          // a node of the active 3D layer's scene (null = none)
    void setSelectedNode(const QUuid& id);
    // Returns the active layer if it has the requested type and is editable; otherwise (optionally)
    // creates one above the active layer. Emits message() when it cannot edit.
    Layer* editableLayer(LayerType t, bool autoCreate);
    QUuid addLayerAboveActive(LayerType t, const QString& name);
    QUuid addTextLayerAboveActive(const TextContent& c, const QString& name);
    QUuid addImageLayerAboveActive(const QImage& img, const QString& name);

    const QSet<QUuid>& selectedShapes() const { return m_selected; }
    void setSelectedShapes(const QSet<QUuid>& s, bool pullStyle = true);
    void clearSelection() { setSelectedShapes({}); }
    // Expand ids to include every shape sharing a group id in the layer.
    QSet<QUuid> expandGroups(const Layer& l, const QSet<QUuid>& ids) const;

    // Tool
    ToolId tool() const { return m_tool; }
    void setTool(ToolId t);

    // Paint settings
    QColor foreground() const { return m_fg; }
    QColor background() const { return m_bg; }
    void setForeground(const QColor& c);
    void setBackground(const QColor& c);
    void swapColors();
    BrushSettings& brushFor(ToolId t);
    BrushSettings brush(ToolId t) const;
    void notifyBrushChanged() { emit brushChanged(); }

    // Vector style
    const VectorStyle& style() const { return m_style; }
    // Changes the default style and, if `applyToSelection`, restyles the selected shapes (undoable).
    void setStyle(const VectorStyle& s, bool applyToSelection = true);
    VectorShape styled(VectorShape s) const;  // apply current style to a new shape

    // Text defaults (used for new text). If the active layer is a text layer, changes are applied to it too.
    const TextContent& textStyle() const { return m_textStyle; }
    void applyTextStyle(const TextContent& style);

    // ---- Animation ----
    Playback* playback() const { return m_playback; }
    const Compositor::OnionSkin& onion() const { return m_onion; }
    void setOnion(const Compositor::OnionSkin& o);   // persisted in the user settings
    Layer* activeAnimationLayer() const;
    // Applies `fn` to a copy of the active animation layer's timing data and records the result as one
    // undo step if it changed anything. Returns false when there is no editable animation layer or fn
    // reported nothing to do.
    bool editActiveAnimation(const QString& undoText, const std::function<bool(AnimationState&)>& fn);
    // Creates a drawing at the current frame (blank or a copy of the visible one), making an animation
    // layer first when the active layer is not one.
    QUuid newDrawing(bool copyVisible);
    // Straight-ahead workflow: step to the next frame and start a fresh drawing there.
    QUuid newDrawingOnNextFrame(bool copyVisible);
    // ---- Frame selection and copy/paste (animation layers) ----
    // The selected frame range on the active animation layer. Moving the playhead any other way, or
    // activating another layer, clears it. With no selection the "selection" is the current frame.
    void setFrameSelection(const QUuid& layer, int first, int last);   // also puts the playhead on `last`
    void clearFrameSelection();
    bool hasFrameSelection() const { return !m_selLayer.isNull() && m_selFirst >= 0; }
    QUuid frameSelectionLayer() const { return m_selLayer; }
    // Range that copy / cut / delete act on (selection, or just the current frame).
    void selectedFrameRange(int* first, int* last) const;
    bool copyFrames(bool cut);
    // Pastes at the current frame, overwriting that many frames. `linked` reuses the copied drawings
    // (same pixels, shared edits) when pasting back onto the layer they came from; otherwise every
    // pasted drawing is an independent copy.
    bool pasteFrames(bool linked);
    const FrameClip& frameClip() const { return m_clip; }
    // Jump to the previous/next exposure that shows a drawing on the active animation layer.
    bool gotoDrawing(int direction);

    // ---- Property animation (keyframes on the transform and opacity of any layer) ----
    // Auto-key: changing a property that is not animated yet creates a keyframe at the current frame
    // (properties that are already animated always key). Off: such changes edit the static value.
    bool autoKey() const { return m_autoKey; }
    void setAutoKey(bool on);
    // Value of a property on `layer` at the current frame.
    PropValue propertyValue(const QUuid& layer, const QString& prop) const;
    // Sets properties of one layer as one undo step. Consecutive calls with the same non-empty mergeKey
    // (a drag) merge into one step. Returns false when nothing was changed (locked layer, unknown id, ...).
    bool setProperties(const QUuid& layer, const QVector<QPair<QString, PropValue>>& values, const QString& mergeKey = {});
    bool setProperty(const QUuid& layer, const QString& prop, PropValue v, const QString& mergeKey = {}) { return setProperties(layer, {{prop, v}}, mergeKey); }
    bool toggleAnimated(const QUuid& layer, const QString& prop);     // stopwatch
    bool addKeysHere(const QUuid& layer, const QString& prop = {});    // key `prop` (or every layer property when empty) at the current frame; never removes
    bool toggleKeyHere(const QUuid& layer, const QString& prop);      // add / remove the key at the current frame
    bool gotoKey(int direction);                                       // previous/next keyframe of the active layer
    // Selection of keyframes (what Delete / Move / Copy / Ease act on).
    const QVector<KeyRef>& selectedKeys() const { return m_keySel; }
    void setKeySelection(const QVector<KeyRef>& keys);
    void clearKeySelection() { setKeySelection({}); }
    bool isKeySelected(const QUuid& layer, const QString& prop, int frame) const;
    // Animate-style tweens: keyframes on Position, Scale, Rotation and Opacity at both ends of a span, taking the
    // layer's values at those frames; the layer then moves between them. Edit the end values (Auto-Key on) to
    // define the motion. One undo step.
    struct TweenOptions {
        enum Kind { Motion, Classic } kind = Motion;
        bool easyEase = true;      // Motion: ease in and out at both ends
        int ease = 0;              // Classic: -100 (ease in, slow start) .. +100 (ease out, slow end)
        int rotateDir = 0;         // 0 = none, +1 clockwise, -1 counter-clockwise
        int spins = 1;             // full turns when rotateDir != 0
    };
    bool createTween(const QUuid& layer, int startFrame, int endFrame, const TweenOptions& o);
    bool removeTween(const QUuid& layer, int startFrame, int endFrame);
    // Shape tween on a vector layer: the layer's shapes are the start shapes, a copy becomes the end shapes and is put
    // into edit mode (tools then change the END shape). "shape.morph" is keyed 0 % -> 100 % over the span.
    bool createShapeTween(const QUuid& layer, int startFrame, int endFrame, bool easyEase);
    bool toggleEditEndShape(const QUuid& layer);           // switch between editing the start and the end shapes
    bool removeShapeTween(const QUuid& layer);              // keeps the start shapes   // removes the keys of those four properties in the span
    bool deleteSelectedKeys();
    bool moveSelectedKeys(int deltaFrames);
    bool easeSelectedKeys(EasePreset preset, bool incoming = true, bool outgoing = true);
    bool copyKeys(bool cut);
    bool pasteKeys();                                                  // onto the active layer, starting at the current frame
    const KeyClip& keyClip() const { return m_keyClip; }
    // True when the most recent copy was of keyframes (so Paste means keyframes, not frames).
    bool keyClipActive() const { return m_keyClipActive && !m_keyClip.isEmpty(); }

    int polygonSides = 5;
    int bucketTolerance = 24;

    // View toggles (persisted by MainWindow)
    bool gridVisible = false, rulersVisible = true, guidesVisible = true, snapEnabled = false;

    // Layer clipboard (within the open document): copies are detached snapshots, so later edits to the original do not change what gets pasted.
    bool copyLayer(const QUuid& id);
    bool cutLayer(const QUuid& id);
    QUuid pasteLayer();                      // above the active layer, same parent; returns the new layer
    bool canPasteLayer() const { return m_doc && m_layerClip; }
signals:
    void documentChanged();
    void activeLayerChanged(const QUuid& id);
    void nodeSelectionChanged(const QUuid& id);
    void selectionChanged();
    void toolChanged(ToolId t);
    void colorsChanged();
    void styleChanged();
    void textStyleChanged();
    void brushChanged();
    void viewTogglesChanged();
    void onionChanged();
    void frameSelectionChanged();
    void clipChanged();
    void keySelectionChanged();
    void autoKeyChanged();
    void keyClipChanged();
    void message(const QString& text);

private:
    Document* m_doc = nullptr;
    QUuid m_activeLayer;
    QUuid m_node;
    LayerPtr m_layerClip;
    QSet<QUuid> m_selected;
    ToolId m_tool = ToolId::Brush;
    QColor m_fg = Qt::black, m_bg = Qt::white;
    BrushSettings m_brush, m_pencil, m_eraser, m_smudge;
    TextContent m_textStyle;
    VectorStyle m_style;
    Playback* m_playback = nullptr;
    QUuid m_selLayer;
    int m_selFirst = -1, m_selLast = -1;
    bool m_selInternal = false;
    FrameClip m_clip;
    bool m_autoKey = false;
    QVector<KeyRef> m_keySel;
    KeyClip m_keyClip;
    bool m_keyClipActive = false;
    Compositor::OnionSkin m_onion;
};

}  // namespace mynd
