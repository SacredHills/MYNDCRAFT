#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QUuid>
#include <QVector>

namespace mynd {

// A Bezier anchor. Handles are stored as offsets relative to `pos`.
struct PathNode {
    QPointF pos;
    QPointF in;   // incoming handle offset (zero = none)
    QPointF out;  // outgoing handle offset (zero = none)
    bool smooth = false;
    bool operator==(const PathNode& o) const {
        return pos == o.pos && in == o.in && out == o.out && smooth == o.smooth;
    }
};

struct GradientStop {
    double t = 0;
    QColor color;
    bool operator==(const GradientStop& o) const { return t == o.t && color == o.color; }
};

struct Paint {
    enum Type { None, Solid, LinearGradient };
    Type type = Solid;
    QColor color = Qt::black;
    QPointF gradStart, gradEnd;  // shape/document space
    QVector<GradientStop> stops;
    bool operator==(const Paint& o) const {
        return type == o.type && color == o.color && gradStart == o.gradStart &&
               gradEnd == o.gradEnd && stops == o.stops;
    }
    QBrush toBrush() const;
};

struct StrokeStyle {
    bool enabled = true;
    QColor color = Qt::black;
    double width = 2.0;
    Qt::PenCapStyle cap = Qt::RoundCap;
    Qt::PenJoinStyle join = Qt::RoundJoin;
    bool operator==(const StrokeStyle& o) const {
        return enabled == o.enabled && color == o.color && width == o.width && cap == o.cap &&
               join == o.join;
    }
    QPen toPen() const;
};

struct VectorShape {
    QUuid id = QUuid::createUuid();
    QUuid groupId;  // null = ungrouped; shapes sharing a groupId select/move together
    QString name;
    QVector<PathNode> nodes;
    bool closed = false;
    Paint fill;
    StrokeStyle stroke;

    QPainterPath path() const;
    QRectF bounds() const;       // geometry bounds including stroke width
    bool hitTest(const QPointF& p, double tolerance) const;
    void transform(const QTransform& t);
    void translate(const QPointF& d);

    // Nearest point on the outline: segment index (segment i runs from node i to node i+1) and
    // curve parameter t in [0,1]; seg = -1 if the path has fewer than 2 nodes.
    struct SegmentHit { int seg = -1; double t = 0; double distance = 1e300; QPointF point; };
    SegmentHit nearestSegment(const QPointF& p) const;
    // Splits segment `seg` at `t` without changing the curve's shape; returns the new node's index.
    int insertNode(int seg, double t);

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& o, VectorShape& out, QString* err);
    bool operator==(const VectorShape& o) const;

    static VectorShape makeRect(const QRectF& r);
    static VectorShape makeEllipse(const QRectF& r);
    static VectorShape makePolygon(const QPointF& center, double radius, int sides, double rotation);
    static VectorShape makeLine(const QPointF& a, const QPointF& b);
};

using ShapeList = QVector<VectorShape>;

QJsonArray shapesToJson(const ShapeList& s);
bool shapesFromJson(const QJsonArray& a, ShapeList& out, QString* err);

}  // namespace mynd
