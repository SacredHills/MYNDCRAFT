#pragma once
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include "Scene/VectorShape.h"

namespace mynd {

// Masks, track matte and effects of one layer (After Effects style). Applied in this order when the layer is drawn:
// effects (in list order) -> masks (in the layer's own space, so they move with the layer) -> layer transform -> track matte -> opacity/blend.
// Values are static (not keyframable).

enum class MaskMode { Add, Subtract, Intersect };

struct Mask {
    QString name = "Mask";
    VectorShape shape;                 // only the outline (nodes, closed) is used; closed shapes only
    MaskMode mode = MaskMode::Add;
    double feather = 0;                // px, soft edge (gaussian)
    double opacity = 100;              // %
    bool invert = false;
    bool operator==(const Mask& o) const { return name == o.name && shape == o.shape && mode == o.mode && feather == o.feather && opacity == o.opacity && invert == o.invert; }
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& o, Mask& out, QString* err);
};

enum class MatteMode { None, Alpha, InvertedAlpha, Luma, InvertedLuma };

enum class EffectType { Blur, DropShadow, Glow, BrightnessContrast, HueSaturation, Invert };

struct EffectInfo {
    EffectType type;
    const char* key;                   // saved
    const char* label;
    int paramCount;
    struct Param { const char* label; double min, max, def; } params[4];
};
const EffectInfo& effectInfo(EffectType t);
const QVector<EffectType>& allEffectTypes();
bool effectTypeFromKey(const QString& key, EffectType& out);

struct Effect {
    EffectType type = EffectType::Blur;
    bool enabled = true;
    double p[4] = {0, 0, 0, 0};        // meaning per type, see effectInfo
    QColor color{0, 0, 0};             // drop shadow colour
    static Effect make(EffectType t);
    bool operator==(const Effect& o) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& o, Effect& out, QString* err);
};

struct LayerFx {
    QVector<Mask> masks;
    QVector<Effect> effects;
    QUuid matteLayer;                  // the layer used as track matte (not drawn by itself); null = none
    MatteMode matteMode = MatteMode::None;
    bool any() const { return !masks.isEmpty() || !effects.isEmpty() || (matteMode != MatteMode::None && !matteLayer.isNull()); }
    bool operator==(const LayerFx& o) const { return masks == o.masks && effects == o.effects && matteLayer == o.matteLayer && matteMode == o.matteMode; }
    QJsonObject toJson() const;
    static LayerFx fromJson(const QJsonObject& o, QStringList* warnings = nullptr);
};

}  // namespace mynd
