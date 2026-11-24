#pragma once
#include <QImage>
#include <QString>
#include <QUuid>
#include <map>
#include <memory>
#include <vector>

#include "Scene/Animation.h"
#include "Scene/Property.h"
#include "Scene/TextContent.h"
#include "Scene/VectorShape.h"

namespace mynd {

enum class LayerType { Raster, Vector, Group, Text, Animation };
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
    TextContent textContent; // Text layers
    // Animation layers: exposure timing plus the pixels of every drawing (canvas-sized ARGB32_Premultiplied).
    // Images stay in the map even when no exposure references them, so undo can bring them back.
    AnimationState anim;
    std::map<QUuid, QImage> drawingImages;
    // Keyframe-animatable properties (transform, opacity). Sparse: absent = default value, no keys.
    PropertyTracks tracks;
    std::vector<LayerPtr> children;  // Group layers
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
    // Bounds of what the layer draws at `frame`, in its own (untransformed) space: opaque pixels, shapes, text,
    // or the union of a group's children (each mapped through its own transform). Empty when there is nothing.
    QRectF contentBounds(int frame, const QSize& canvas) const;
    // Deep copy with fresh ids (for duplicate).
    LayerPtr cloneWithNewIds() const;
    // Deep copy keeping ids (for undo snapshots).
    LayerPtr cloneExact() const;
};

}  // namespace mynd
