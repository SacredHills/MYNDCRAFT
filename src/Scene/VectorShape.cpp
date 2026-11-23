#include "Scene/VectorShape.h"

#include <QJsonArray>
#include <QLinearGradient>
#include <QPainterPathStroker>
#include <QPen>
#include <cmath>

namespace mynd {

namespace {
QJsonArray pt(const QPointF& p) { return QJsonArray{p.x(), p.y()}; }
QPointF readPt(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a[0].toDouble(), a[1].toDouble()) : QPointF();
}
QString col(const QColor& c) { return c.name(QColor::HexArgb); }
QColor readCol(const QJsonValue& v, const QColor& def) {
    QColor c(v.toString());
    return c.isValid() ? c : def;
}
}  // namespace

QBrush Paint::toBrush() const {
    switch (type) {
    case None: return Qt::NoBrush;
    case Solid: return QBrush(color);
    case LinearGradient: {
        QLinearGradient g(gradStart, gradEnd);
        g.setCoordinateMode(QGradient::LogicalMode);
        for (const auto& s : stops) g.setColorAt(qBound(0.0, s.t, 1.0), s.color);
        if (stops.isEmpty()) { g.setColorAt(0, color); g.setColorAt(1, color); }
        return QBrush(g);
    }
    }
    return Qt::NoBrush;
}

QPen StrokeStyle::toPen() const {
    if (!enabled || width <= 0) return Qt::NoPen;
    QPen p(color, width, Qt::SolidLine, cap, join);
    return p;
}

QPainterPath VectorShape::path() const {
    QPainterPath p;
    if (nodes.isEmpty()) return p;
    p.moveTo(nodes[0].pos);
    const int n = nodes.size();
    const int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        const PathNode& a = nodes[i];
        const PathNode& b = nodes[(i + 1) % n];
        if (a.out.isNull() && b.in.isNull()) p.lineTo(b.pos);
        else p.cubicTo(a.pos + a.out, b.pos + b.in, b.pos);
    }
    if (closed) p.closeSubpath();
    return p;
}

QRectF VectorShape::bounds() const {
    QRectF r = path().boundingRect();
    if (stroke.enabled) {
        const double h = stroke.width / 2.0 + 1.0;
        r.adjust(-h, -h, h, h);
    }
    return r;
}

bool VectorShape::hitTest(const QPointF& p, double tol) const {
    const QPainterPath pp = path();
    if (closed && fill.type != Paint::None && pp.contains(p)) return true;
    QPainterPathStroker s;
    s.setWidth(std::max(stroke.enabled ? stroke.width : 0.0, 0.0) + 2 * tol);
    return s.createStroke(pp).contains(p);
}

void VectorShape::transform(const QTransform& t) {
    for (auto& n : nodes) {
        const QPointF np = t.map(n.pos);
        n.in = n.in.isNull() ? n.in : t.map(n.pos + n.in) - np;
        n.out = n.out.isNull() ? n.out : t.map(n.pos + n.out) - np;
        n.pos = np;
    }
    fill.gradStart = t.map(fill.gradStart);
    fill.gradEnd = t.map(fill.gradEnd);
}

void VectorShape::translate(const QPointF& d) {
    for (auto& n : nodes) n.pos += d;
    fill.gradStart += d;
    fill.gradEnd += d;
}

bool VectorShape::operator==(const VectorShape& o) const {
    return id == o.id && groupId == o.groupId && name == o.name && nodes == o.nodes &&
           closed == o.closed && fill == o.fill && stroke == o.stroke;
}

QJsonObject VectorShape::toJson() const {
    QJsonObject o;
    o["id"] = id.toString(QUuid::WithoutBraces);
    if (!groupId.isNull()) o["group"] = groupId.toString(QUuid::WithoutBraces);
    o["name"] = name;
    o["closed"] = closed;
    QJsonArray ns;
    for (const auto& n : nodes) {
        QJsonObject no;
        no["p"] = pt(n.pos);
        if (!n.in.isNull()) no["in"] = pt(n.in);
        if (!n.out.isNull()) no["out"] = pt(n.out);
        if (n.smooth) no["smooth"] = true;
        ns.append(no);
    }
    o["nodes"] = ns;
    QJsonObject f;
    f["type"] = int(fill.type);
    f["color"] = col(fill.color);
    if (fill.type == Paint::LinearGradient) {
        f["start"] = pt(fill.gradStart);
        f["end"] = pt(fill.gradEnd);
        QJsonArray st;
        for (const auto& s : fill.stops) st.append(QJsonObject{{"t", s.t}, {"c", col(s.color)}});
        f["stops"] = st;
    }
    o["fill"] = f;
    QJsonObject s;
    s["enabled"] = stroke.enabled;
    s["color"] = col(stroke.color);
    s["width"] = stroke.width;
    s["cap"] = int(stroke.cap);
    s["join"] = int(stroke.join);
    o["stroke"] = s;
    return o;
}

bool VectorShape::fromJson(const QJsonObject& o, VectorShape& out, QString* err) {
    VectorShape s;
    s.id = QUuid(o["id"].toString());
    if (s.id.isNull()) { if (err) *err = "vector shape without valid id"; return false; }
    s.groupId = QUuid(o["group"].toString());
    s.name = o["name"].toString();
    s.closed = o["closed"].toBool();
    for (const auto& v : o["nodes"].toArray()) {
        const QJsonObject no = v.toObject();
        PathNode n;
        n.pos = readPt(no["p"]);
        n.in = readPt(no["in"]);
        n.out = readPt(no["out"]);
        n.smooth = no["smooth"].toBool();
        s.nodes.append(n);
    }
    const QJsonObject f = o["fill"].toObject();
    const int ft = f["type"].toInt(Paint::Solid);
    if (ft < Paint::None || ft > Paint::LinearGradient) { if (err) *err = "bad fill type"; return false; }
    s.fill.type = Paint::Type(ft);
    s.fill.color = readCol(f["color"], Qt::black);
    s.fill.gradStart = readPt(f["start"]);
    s.fill.gradEnd = readPt(f["end"]);
    for (const auto& v : f["stops"].toArray()) {
        const QJsonObject so = v.toObject();
        s.fill.stops.append({so["t"].toDouble(), readCol(so["c"], Qt::black)});
    }
    const QJsonObject st = o["stroke"].toObject();
    s.stroke.enabled = st["enabled"].toBool(true);
    s.stroke.color = readCol(st["color"], Qt::black);
    s.stroke.width = st["width"].toDouble(2.0);
    s.stroke.cap = Qt::PenCapStyle(st["cap"].toInt(Qt::RoundCap));
    s.stroke.join = Qt::PenJoinStyle(st["join"].toInt(Qt::RoundJoin));
    out = s;
    return true;
}

QJsonArray shapesToJson(const ShapeList& s) {
    QJsonArray a;
    for (const auto& x : s) a.append(x.toJson());
    return a;
}
bool shapesFromJson(const QJsonArray& a, ShapeList& out, QString* err) {
    ShapeList r;
    for (const auto& v : a) {
        VectorShape s;
        if (!VectorShape::fromJson(v.toObject(), s, err)) return false;
        r.append(s);
    }
    out = r;
    return true;
}

VectorShape VectorShape::makeRect(const QRectF& r) {
    VectorShape s;
    s.name = "Rectangle";
    s.closed = true;
    for (const QPointF& p : {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}) {
        PathNode n; n.pos = p; s.nodes.append(n);
    }
    return s;
}

VectorShape VectorShape::makeEllipse(const QRectF& r) {
    VectorShape s;
    s.name = "Ellipse";
    s.closed = true;
    const double k = 0.5522847498307936;
    const QPointF c = r.center();
    const double rx = r.width() / 2, ry = r.height() / 2;
    // top, right, bottom, left (clockwise)
    const QPointF pos[4] = {{c.x(), c.y() - ry}, {c.x() + rx, c.y()}, {c.x(), c.y() + ry}, {c.x() - rx, c.y()}};
    const QPointF out[4] = {{rx * k, 0}, {0, ry * k}, {-rx * k, 0}, {0, -ry * k}};
    for (int i = 0; i < 4; ++i) {
        PathNode n; n.pos = pos[i]; n.out = out[i]; n.in = -out[i]; n.smooth = true;
        s.nodes.append(n);
    }
    return s;
}

VectorShape VectorShape::makePolygon(const QPointF& c, double radius, int sides, double rot) {
    VectorShape s;
    s.name = "Polygon";
    s.closed = true;
    sides = std::max(3, sides);
    for (int i = 0; i < sides; ++i) {
        const double a = rot + 2 * M_PI * i / sides;
        PathNode n; n.pos = c + QPointF(std::cos(a), std::sin(a)) * radius;
        s.nodes.append(n);
    }
    return s;
}

VectorShape VectorShape::makeLine(const QPointF& a, const QPointF& b) {
    VectorShape s;
    s.name = "Line";
    s.closed = false;
    s.fill.type = Paint::None;
    PathNode na; na.pos = a; PathNode nb; nb.pos = b;
    s.nodes = {na, nb};
    return s;
}

}  // namespace mynd
