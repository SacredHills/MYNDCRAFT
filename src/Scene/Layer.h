#pragma once
#include <QImage>
#include <QString>
#include <QUuid>
#include <memory>
#include <vector>

#include "Scene/TextContent.h"
#include "Scene/VectorShape.h"

namespace mynd {

enum class LayerType { Raster, Vector, Group, Text };
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
    std::vector<LayerPtr> children;  // Group layers
    Layer* parent = nullptr;         // maintained by Document

    LayerProps properties() const { return props; }
    bool isGroup() const { return type == LayerType::Group; }
    // Deep copy with fresh ids (for duplicate).
    LayerPtr cloneWithNewIds() const;
    // Deep copy keeping ids (for undo snapshots).
    LayerPtr cloneExact() const;
};

}  // namespace mynd
