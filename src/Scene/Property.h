#pragma once
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

namespace mynd {

// ---------------------------------------------------------------------------------------------
// Typed, keyframe-animatable properties. One engine for every animated value in the project: a
// property has a persistent id, a data type, a default, an optional keyframe list and per-key
// interpolation. Values are stored as up to four doubles; the PropType says how to read them
// (and how to interpolate/validate them). Pure data + evaluation: no UI, no undo (see Document).
// ---------------------------------------------------------------------------------------------

enum class PropType { Float, Int, Bool, Vec2, Vec3, Vec4, Color, Rotation, Enum };
int propComponents(PropType t);
QString propTypeKey(PropType t);

struct PropValue {
    double c[4] = {0, 0, 0, 0};
    static PropValue of(double a, double b = 0, double c2 = 0, double d = 0) { PropValue v; v.c[0] = a; v.c[1] = b; v.c[2] = c2; v.c[3] = d; return v; }
    bool operator==(const PropValue& o) const { return c[0] == o.c[0] && c[1] == o.c[1] && c[2] == o.c[2] && c[3] == o.c[3]; }
    bool operator!=(const PropValue& o) const { return !(*this == o); }
};

struct PropertyDef {
    QString id;                 // persistent identifier, e.g. "transform.position"
    QString label;              // shown in the UI
    QString group;              // "Transform", ...
    PropType type = PropType::Float;
    PropValue def;              // default value
    PropValue minV, maxV;       // validation range per component
    QStringList componentNames; // e.g. {"X","Y"}
    QString unit;               // "px", "%", "°"
    int decimals = 1;
};

// Properties every layer has. The list order is the display order.
const QVector<PropertyDef>& layerPropertyDefs();
const PropertyDef* findPropertyDef(const QString& id);
PropValue clampValue(const PropertyDef& d, PropValue v);   // range + type rules (int rounding, bool 0/1)

enum class Interp { Linear, Hold, Bezier };
QString interpKey(Interp i);
Interp interpFromKey(const QString& k, bool* ok = nullptr);

// Temporal Bezier handle in segment-normalised coordinates: `x` is the share of the segment's duration
// (0..1), `y` the share of the value change (may leave 0..1 to overshoot). A linear side behaves like
// the handle (1/3, 1/3).
struct Handle {
    double x = 1.0 / 3.0, y = 1.0 / 3.0;
    bool operator==(const Handle& o) const { return x == o.x && y == o.y; }
};

struct Keyframe {
    int frame = 0;
    PropValue value;
    Interp in = Interp::Linear;     // how the segment arriving at this key behaves (its end of the curve)
    Interp out = Interp::Linear;    // how the segment leaving this key behaves
    Handle inHandle, outHandle;     // used for the sides that are Bezier
    // Spatial tangents (Vec2 properties such as Position): offsets, in value units, of the path's control
    // points from this key. (0,0) on both ends of a segment keeps the path a straight line.
    QPointF spatialIn, spatialOut;
    bool operator==(const Keyframe& o) const {
        return frame == o.frame && value == o.value && in == o.in && out == o.out && inHandle == o.inHandle && outHandle == o.outHandle
            && spatialIn == o.spatialIn && spatialOut == o.spatialOut;
    }
};

// Presets for the temporal interpolation of one keyframe.
enum class EasePreset { Linear, Hold, EaseIn, EaseOut, EaseInOut };

struct PropertyTrack {
    PropValue base;                 // value while there are no keyframes
    QVector<Keyframe> keys;         // sorted by frame, one key per frame
    bool animated() const { return !keys.isEmpty(); }
    int indexOfKey(int frame) const;
    PropValue valueAt(const PropertyDef& d, double frame) const;
    // Rate of change per frame (for Vec2: length of the velocity vector, in value units / frame); used by the speed graph.
    double speedAt(const PropertyDef& d, double frame) const;
    // Pure edits. All values are validated through clampValue.
    void setKey(const PropertyDef& d, int frame, PropValue v);      // add, or change the value of an existing key
    bool removeKey(int frame);
    bool moveKey(int from, int to);                                  // an existing key at `to` is replaced
    bool applyEase(int frame, EasePreset p, bool incoming, bool outgoing);
    bool operator==(const PropertyTrack& o) const { return base == o.base && keys == o.keys; }
};

// Sparse: a property that is not in the map has its default value and no keys.
using PropertyTracks = QMap<QString, PropertyTrack>;

PropValue valueOf(const PropertyTracks& t, const PropertyDef& d, double frame);
// Change a property's value at `frame`. Animated properties (or forceKey) get a keyframe there; otherwise
// the static value changes. Unknown ids are ignored.
void setValueAt(PropertyTracks& t, const QString& id, double frame, PropValue v, bool forceKey);
void enableAnimation(PropertyTracks& t, const QString& id, int frame);    // stopwatch on: key with the current value
void disableAnimation(PropertyTracks& t, const QString& id, int frame);   // stopwatch off: freeze the value at `frame`
void pruneDefaults(PropertyTracks& t);                                    // drop entries equal to the default

// Evaluated layer transform. Scale/opacity are fractions (1 = 100 %). Position is how far the anchor point
// has moved from where it sits when untransformed; anchor is measured from the canvas centre.
struct LayerXform {
    QPointF position, anchor;
    QPointF scale{1, 1};
    double rotation = 0;            // degrees, clockwise on screen
    double opacity = 1;
    bool isIdentity() const { return position.isNull() && anchor.isNull() && scale == QPointF(1, 1) && rotation == 0; }
    QTransform matrix(const QPointF& canvasCenter) const;
};
LayerXform evaluateTransform(const PropertyTracks& t, double frame);

// Same layer-space fraction at which temporal Bezier easing sits at time fraction u (exposed for tests and
// the graph editor). Handles are the effective P1/P2 control points' x,y in [0,1] x R.
double bezierEase(double u, double x1, double y1, double x2, double y2);

// ---- serialisation ----
QJsonObject tracksToJson(const PropertyTracks& t);
// Unknown property ids are skipped and reported in *warnings; malformed data fails with *err.
bool tracksFromJson(const QJsonObject& o, PropertyTracks& out, QString* err, QStringList* warnings = nullptr);

constexpr int kMaxKeyframesPerProperty = 100000;

}  // namespace mynd
