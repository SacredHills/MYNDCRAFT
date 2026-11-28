#pragma once
#include "Scene/LayerFx.h"
#include <QImage>
#include <QString>
#include <QUuid>
#include <map>
#include <memory>
#include <vector>

#include "Scene/Animation.h"
#include "Scene/Property.h"
#include "Scene3D/Scene3D.h"
#include "Scene/TextContent.h"
#include "Scene/VectorShape.h"

namespace mynd {

enum class LayerType { Raster, Vector, Group, Text, Animation, Symbol, Scene3D };
// Symbols: reusable artwork with its own timeline. A Graphic symbol's time follows the timeline it sits in (with
// loop / play-once / single-frame control); a Movie Clip runs on its own and always loops from its first frame.
enum class SymbolKind { Graphic, MovieClip };
enum class SymbolPlay { Loop, PlayOnce, SingleFrame };
QString symbolKindKey(SymbolKind k);
bool symbolKindFromKey(const QString& s, SymbolKind& out);
QString symbolPlayKey(SymbolPlay p);
bool symbolPlayFromKey(const QString& s, SymbolPlay& out);
enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten, Add, Difference };

QString blendModeName(BlendMode m);
QString blendModeKey(BlendMode m);
BlendMode blendModeFromKey(const QString& k, bool* ok = nullptr);
QString layerTypeKey(LayerType t);
bool layerTypeFromKey(const QString& k, LayerType& out);
const std::vector<BlendMode>& allBlendModes();

struct LayerProps {
    QString name;
    bool visible = true;
    bool locked = false;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    bool operator==(const LayerProps& o) const {
        return name == o.name && visible == o.visible && locked == o.locked &&
               opacity == o.opacity && blend == o.blend;
    }
};

class Layer;
using LayerPtr = std::shared_ptr<Layer>;

// One node of the layer tree. Children are ordered bottom -> top.
class Layer {
public:
    Layer(LayerType type, const QString& name);

    QUuid id;
    LayerType type;
    LayerProps props;
    QImage raster;          // Raster layers: canvas-sized ARGB32_Premultiplied
    ShapeList shapes;       // Vector layers: bottom -> top
    // Shape tween: the end shapes (paired with `shapes` by position). The animatable property "shape.morph" (0-100 %)
    // blends between them. While `editingEnd` is set the two lists are swapped so the tools edit the end shapes.
    // More than two shapes: `morphExtra` holds shape 3, 4, ... (poses 2, 3, ...); "shape.morph" 0 % = shape 1, 100 % = shape 2, 200 % = shape 3 ...
    // and values in between blend the two neighbouring shapes. While a pose other than the first is being edited (`editingPose` > 0) the tools
    // work on `shapes`, which then holds that pose, and the first pose sits in that pose's slot instead.
    ShapeList morphShapes;
    QVector<ShapeList> morphExtra;
    int editingPose = 0;
    bool editingEnd() const { return editingPose != 0; }
    int poseCount() const { return hasShapeTween() ? 2 + int(morphExtra.size()) : 1; }
    QVector<ShapeList> poses() const;                                   // the shapes of every pose in order, whatever is being edited
    struct PoseLayout { ShapeList shapes, morph; QVector<ShapeList> extra; int editing = 0; };
    static PoseLayout layoutPoses(const QVector<ShapeList>& poses, int editing);
    TextContent textContent; // Text layers
    // Animation layers: exposure timing plus the pixels of every drawing (canvas-sized ARGB32_Premultiplied).
    // Images stay in the map even when no exposure references them, so undo can bring them back.
    AnimationState anim;
    std::map<QUuid, QImage> drawingImages;
    // Keyframe-animatable properties (transform, opacity). Sparse: absent = default value, no keys.
    PropertyTracks tracks;
    LayerFx fx;                      // masks, track matte, effects (all layer types)
    std::vector<LayerPtr> children;  // Group layers
    // Symbol instances: which symbol is shown and how its timeline is mapped onto this one.
    QUuid symbolId;
    SymbolPlay symbolPlay = SymbolPlay::Loop;
    int symbolFirstFrame = 0;
    // Scene3D layers: a 3D scene rendered through its active camera into the composition (never null for that type).
    std::shared_ptr<mynd::Scene3D> scene3d;
    // The 3D scene as it looks at `frame` (animated properties overlaid); the stored scene itself when nothing is animated.
    std::shared_ptr<const mynd::Scene3D> sceneAt(double frame) const;
    // Animated 3D properties of this layer's scene (ids understood by findPropertyDef), in scene order; empty for other layers.
    QVector<QString> animated3DProperties() const;
    // Display name of any property id of this layer ("Position", "Cube › Position").
    QString propertyLabel(const QString& id) const;
    Layer* parent = nullptr;         // maintained by Document

    LayerProps properties() const { return props; }
    bool isGroup() const { return type == LayerType::Group; }
    // Pixel layers hold a bitmap per frame: one fixed bitmap (Raster) or one per exposed drawing (Animation).
    bool isPixelLayer() const { return type == LayerType::Raster || type == LayerType::Animation; }
    // The bitmap shown at `frame`; null for non-pixel layers and for blank frames of animation layers.
    const QImage* imageAt(int frame) const;
    QImage* imageForEdit(int frame);
    QImage* drawingImage(const QUuid& id);
    // Transform/opacity evaluated at `frame` (identity when nothing is set).
    LayerXform xformAt(double frame) const { return evaluateTransform(tracks, frame); }
    // The shapes to draw at `frame` (the morph blend for shape tweens, otherwise `shapes`).
    ShapeList shapesAt(double frame) const;
    bool hasShapeTween() const { return type == LayerType::Vector && !morphShapes.isEmpty(); }
    // Bounds of what the layer draws at `frame`, in its own (untransformed) space: opaque pixels, shapes, text,
    // or the union of a group's children (each mapped through its own transform). Empty when there is nothing.
    QRectF contentBounds(int frame, const QSize& canvas, const struct SymbolResolver* syms = nullptr) const;
    // Deep copy with fresh ids (for duplicate).
    LayerPtr cloneWithNewIds() const;
    // Deep copy keeping ids (for undo snapshots).
    LayerPtr cloneExact() const;
};

// A library symbol: a named layer tree (`root`, a group that owns the children) plus its own length.
struct SymbolDef {
    QUuid id;
    QString name;
    SymbolKind kind = SymbolKind::Graphic;
    int durationFrames = 24;
    LayerPtr root;
    bool operator==(const SymbolDef& o) const {
        return id == o.id && name == o.name && kind == o.kind && durationFrames == o.durationFrames && root == o.root;
    }
};
struct SymbolResolver {
    virtual ~SymbolResolver() = default;
    virtual const SymbolDef* findSymbol(const QUuid& id) const = 0;
};
// Frame of the symbol's own timeline that an instance shows while its parent timeline is at `parentFrame`.
int symbolLocalFrame(const Layer& instance, const SymbolDef& def, int parentFrame);

}  // namespace mynd
