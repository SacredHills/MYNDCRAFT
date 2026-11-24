#include "Scene/Property.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace mynd {

int propComponents(PropType t) {
    switch (t) {
    case PropType::Float: case PropType::Int: case PropType::Bool: case PropType::Rotation: case PropType::Enum: return 1;
    case PropType::Vec2: return 2;
    case PropType::Vec3: return 3;
    case PropType::Vec4: case PropType::Color: return 4;
    }
    return 1;
}
QString propTypeKey(PropType t) {
    switch (t) {
    case PropType::Float: return "float"; case PropType::Int: return "int"; case PropType::Bool: return "bool";
    case PropType::Vec2: return "vec2"; case PropType::Vec3: return "vec3"; case PropType::Vec4: return "vec4";
    case PropType::Color: return "color"; case PropType::Rotation: return "rotation"; case PropType::Enum: return "enum";
    }
    return "float";
}

static PropertyDef mkDef(const char* id, const char* label, PropType t, PropValue def, double lo, double hi, QStringList names,
                         const char* unit, int dec) {
    PropertyDef d;
    d.id = id; d.label = label; d.group = "Transform"; d.type = t; d.def = def;
    for (int i = 0; i < 4; ++i) { d.minV.c[i] = lo; d.maxV.c[i] = hi; }
    d.componentNames = names; d.unit = unit; d.decimals = dec;
    return d;
}

const QVector<PropertyDef>& layerPropertyDefs() {
    static const QVector<PropertyDef> defs = [] {
        QVector<PropertyDef> v;
        v.push_back(mkDef("transform.anchor", "Anchor Point", PropType::Vec2, PropValue::of(0, 0), -1e6, 1e6, {"X", "Y"}, "px", 1));
        v.push_back(mkDef("transform.position", "Position", PropType::Vec2, PropValue::of(0, 0), -1e6, 1e6, {"X", "Y"}, "px", 1));
        v.push_back(mkDef("transform.scale", "Scale", PropType::Vec2, PropValue::of(100, 100), -10000, 10000, {"X", "Y"}, "%", 1));
        v.push_back(mkDef("transform.rotation", "Rotation", PropType::Rotation, PropValue::of(0), -1e6, 1e6, {"°"}, "°", 1));
        PropertyDef op = mkDef("opacity", "Opacity", PropType::Float, PropValue::of(100), 0, 100, {"%"}, "%", 1);
        op.group = "Layer";
        v.push_back(op);
        PropertyDef mo = mkDef("shape.morph", "Shape Morph", PropType::Float, PropValue::of(0), 0, 100, {"%"}, "%", 1);
        mo.group = "Shape";
        v.push_back(mo);
        return v;
    }();
    return defs;
}

const PropertyDef* findPropertyDef(const QString& id) {
    for (const PropertyDef& d : layerPropertyDefs()) if (d.id == id) return &d;
    return nullptr;
}

PropValue clampValue(const PropertyDef& d, PropValue v) {
    const int n = propComponents(d.type);
    for (int i = 0; i < 4; ++i) {
        if (i >= n) { v.c[i] = 0; continue; }
        double x = std::isfinite(v.c[i]) ? v.c[i] : d.def.c[i];
        x = std::clamp(x, d.minV.c[i], d.maxV.c[i]);
        if (d.type == PropType::Int || d.type == PropType::Enum) x = std::round(x);
        if (d.type == PropType::Bool) x = x != 0 ? 1 : 0;
        v.c[i] = x;
    }
    return v;
}

QString interpKey(Interp i) { return i == Interp::Hold ? "hold" : i == Interp::Bezier ? "bezier" : "linear"; }
Interp interpFromKey(const QString& k, bool* ok) {
    if (ok) *ok = true;
    if (k == "linear") return Interp::Linear;
    if (k == "hold") return Interp::Hold;
    if (k == "bezier") return Interp::Bezier;
    if (ok) *ok = false;
    return Interp::Linear;
}

// ---------------------------------------------------------------- evaluation
double bezierEase(double u, double x1, double y1, double x2, double y2) {
    if (u <= 0) return 0;
    if (u >= 1) return 1;
    x1 = std::clamp(x1, 0.0, 1.0); x2 = std::clamp(x2, 0.0, 1.0);
    auto bx = [&](double s) { const double m = 1 - s; return 3 * m * m * s * x1 + 3 * m * s * s * x2 + s * s * s; };
    auto by = [&](double s) { const double m = 1 - s; return 3 * m * m * s * y1 + 3 * m * s * s * y2 + s * s * s; };
    auto dbx = [&](double s) { const double m = 1 - s; return 3 * m * m * x1 + 6 * m * s * (x2 - x1) + 3 * s * s * (1 - x2); };
    double s = u;
    for (int i = 0; i < 8; ++i) {                       // Newton
        const double e = bx(s) - u;
        if (std::fabs(e) < 1e-10) return by(s);
        const double d = dbx(s);
        if (std::fabs(d) < 1e-9) break;
        s -= e / d;
        if (s < 0 || s > 1) break;
    }
    double lo = 0, hi = 1;                              // bisection fallback (x(s) is monotone for x in [0,1])
    s = u;
    for (int i = 0; i < 60; ++i) {
        const double e = bx(s) - u;
        if (std::fabs(e) < 1e-10) break;
        if (e < 0) lo = s; else hi = s;
        s = (lo + hi) / 2;
    }
    return by(s);
}

int PropertyTrack::indexOfKey(int frame) const {
    auto it = std::lower_bound(keys.begin(), keys.end(), frame, [](const Keyframe& k, int f) { return k.frame < f; });
    return it != keys.end() && it->frame == frame ? int(it - keys.begin()) : -1;
}

PropValue PropertyTrack::valueAt(const PropertyDef& d, double frame) const {
    if (keys.isEmpty()) return base;
    if (frame <= keys.first().frame) return keys.first().value;
    if (frame >= keys.last().frame) return keys.last().value;
    auto it = std::upper_bound(keys.begin(), keys.end(), frame, [](double f, const Keyframe& k) { return f < k.frame; });
    const Keyframe& b = *it;
    const Keyframe& a = *(it - 1);
    if (frame == a.frame) return a.value;
    const bool discrete = d.type == PropType::Bool || d.type == PropType::Enum;
    if (discrete || a.out == Interp::Hold) return a.value;
    const double u = (frame - a.frame) / double(b.frame - a.frame);
    double e = u;
    if (a.out == Interp::Bezier || b.in == Interp::Bezier) {
        const Handle o = a.out == Interp::Bezier ? a.outHandle : Handle{};
        const Handle i = b.in == Interp::Bezier ? b.inHandle : Handle{};
        e = bezierEase(u, o.x, o.y, 1 - i.x, 1 - i.y);
    }
    PropValue r;
    for (int c = 0; c < propComponents(d.type); ++c) r.c[c] = a.value.c[c] + (b.value.c[c] - a.value.c[c]) * e;
    if (d.type == PropType::Vec2 && (!a.spatialOut.isNull() || !b.spatialIn.isNull())) {
        // curved path: cubic Bezier through the two keys, travelled according to the temporal ease
        const QPointF p0(a.value.c[0], a.value.c[1]), p3(b.value.c[0], b.value.c[1]);
        // a side without a tangent uses the straight-line default (a third of the way to the other key)
        const QPointF p1 = a.spatialOut.isNull() ? p0 + (p3 - p0) / 3.0 : p0 + a.spatialOut;
        const QPointF p2 = b.spatialIn.isNull() ? p3 - (p3 - p0) / 3.0 : p3 + b.spatialIn;
        const double m = 1 - e;
        const QPointF q = m * m * m * p0 + 3 * m * m * e * p1 + 3 * m * e * e * p2 + e * e * e * p3;
        r.c[0] = q.x(); r.c[1] = q.y();
    }
    if (d.type == PropType::Int) for (int c = 0; c < 4; ++c) r.c[c] = std::round(r.c[c]);
    return r;
}

double PropertyTrack::speedAt(const PropertyDef& d, double frame) const {
    if (keys.size() < 2) return 0;
    const double h = 0.01;
    const double f0 = std::max<double>(keys.first().frame, frame - h), f1 = std::min<double>(keys.last().frame, frame + h);
    if (f1 <= f0) return 0;
    const PropValue a = valueAt(d, f0), b = valueAt(d, f1);
    double sum = 0;
    for (int c = 0; c < propComponents(d.type); ++c) sum += (b.c[c] - a.c[c]) * (b.c[c] - a.c[c]);
    return std::sqrt(sum) / (f1 - f0);
}

void PropertyTrack::setKey(const PropertyDef& d, int frame, PropValue v) {
    v = clampValue(d, v);
    auto it = std::lower_bound(keys.begin(), keys.end(), frame, [](const Keyframe& k, int f) { return k.frame < f; });
    if (it != keys.end() && it->frame == frame) { it->value = v; return; }
    Keyframe k; k.frame = frame; k.value = v;
    keys.insert(it, k);
}

bool PropertyTrack::removeKey(int frame) {
    const int i = indexOfKey(frame);
    if (i < 0) return false;
    keys.remove(i);
    return true;
}

bool PropertyTrack::moveKey(int from, int to) {
    const int i = indexOfKey(from);
    if (i < 0 || to < 0) return false;
    if (from == to) return true;
    Keyframe k = keys[i];
    keys.remove(i);
    k.frame = to;
    auto it = std::lower_bound(keys.begin(), keys.end(), to, [](const Keyframe& kk, int f) { return kk.frame < f; });
    if (it != keys.end() && it->frame == to) *it = k; else keys.insert(it, k);
    return true;
}

bool PropertyTrack::applyEase(int frame, EasePreset p, bool incoming, bool outgoing) {
    const int i = indexOfKey(frame);
    if (i < 0) return false;
    Keyframe& k = keys[i];
    auto setSide = [&](bool in, Interp it, Handle h) { if (in) { k.in = it; k.inHandle = h; } else { k.out = it; k.outHandle = h; } };
    for (bool in : {true, false}) {
        if (in ? !incoming : !outgoing) continue;
        switch (p) {
        case EasePreset::Linear: setSide(in, Interp::Linear, Handle{}); break;
        case EasePreset::Hold: setSide(in, Interp::Hold, Handle{}); break;
        case EasePreset::EaseIn: if (in) setSide(true, Interp::Bezier, Handle{1.0 / 3.0, 0}); break;      // only the incoming side, like After Effects
        case EasePreset::EaseOut: if (!in) setSide(false, Interp::Bezier, Handle{1.0 / 3.0, 0}); break;
        case EasePreset::EaseInOut: setSide(in, Interp::Bezier, Handle{1.0 / 3.0, 0}); break;
        }
    }
    return true;
}

PropValue valueOf(const PropertyTracks& t, const PropertyDef& d, double frame) {
    auto it = t.constFind(d.id);
    return it == t.constEnd() ? d.def : it->valueAt(d, frame);
}

static PropertyTrack& trackFor(PropertyTracks& t, const PropertyDef& d) {
    auto it = t.find(d.id);
    if (it == t.end()) { PropertyTrack tr; tr.base = d.def; it = t.insert(d.id, tr); }
    return *it;
}

void setValueAt(PropertyTracks& t, const QString& id, double frame, PropValue v, bool forceKey) {
    const PropertyDef* d = findPropertyDef(id);
    if (!d) return;
    PropertyTrack& tr = trackFor(t, *d);
    if (tr.animated() || forceKey) tr.setKey(*d, int(std::lround(frame)), v);
    else tr.base = clampValue(*d, v);
    pruneDefaults(t);
}

void enableAnimation(PropertyTracks& t, const QString& id, int frame) {
    const PropertyDef* d = findPropertyDef(id);
    if (!d) return;
    PropertyTrack& tr = trackFor(t, *d);
    if (tr.animated()) return;
    tr.setKey(*d, frame, tr.base);
}

void disableAnimation(PropertyTracks& t, const QString& id, int frame) {
    const PropertyDef* d = findPropertyDef(id);
    if (!d) return;
    PropertyTrack& tr = trackFor(t, *d);
    tr.base = clampValue(*d, tr.valueAt(*d, frame));
    tr.keys.clear();
    pruneDefaults(t);
}

void pruneDefaults(PropertyTracks& t) {
    for (auto it = t.begin(); it != t.end();) {
        const PropertyDef* d = findPropertyDef(it.key());
        if (!d || (it->keys.isEmpty() && it->base == d->def)) it = t.erase(it); else ++it;
    }
}

QTransform LayerXform::matrix(const QPointF& c) const {
    const QPointF a = c + anchor;
    QTransform m;
    m.translate(a.x() + position.x(), a.y() + position.y());
    m.rotate(rotation);
    m.scale(scale.x(), scale.y());
    m.translate(-a.x(), -a.y());
    return m;
}

LayerXform evaluateTransform(const PropertyTracks& t, double frame) {
    LayerXform x;
    if (t.isEmpty()) return x;
    auto v = [&](const char* id) { return valueOf(t, *findPropertyDef(id), frame); };
    const PropValue a = v("transform.anchor"), p = v("transform.position"), s = v("transform.scale");
    x.anchor = {a.c[0], a.c[1]};
    x.position = {p.c[0], p.c[1]};
    x.scale = {s.c[0] / 100.0, s.c[1] / 100.0};
    x.rotation = v("transform.rotation").c[0];
    x.opacity = v("opacity").c[0] / 100.0;
    return x;
}

// ---------------------------------------------------------------- JSON
QJsonObject tracksToJson(const PropertyTracks& t) {
    QJsonObject o;
    for (auto it = t.begin(); it != t.end(); ++it) {
        const PropertyDef* d = findPropertyDef(it.key());
        if (!d) continue;
        const int n = propComponents(d->type);
        auto arr = [&](const PropValue& v) { QJsonArray a; for (int i = 0; i < n; ++i) a.append(v.c[i]); return a; };
        QJsonObject tr;
        tr["type"] = propTypeKey(d->type);
        tr["base"] = arr(it->base);
        QJsonArray keys;
        for (const Keyframe& k : it->keys) {
            QJsonObject ko;
            ko["frame"] = k.frame;
            ko["value"] = arr(k.value);
            ko["in"] = interpKey(k.in); ko["out"] = interpKey(k.out);
            if (k.in == Interp::Bezier) ko["inHandle"] = QJsonArray{k.inHandle.x, k.inHandle.y};
            if (k.out == Interp::Bezier) ko["outHandle"] = QJsonArray{k.outHandle.x, k.outHandle.y};
            if (!k.spatialIn.isNull()) ko["spatialIn"] = QJsonArray{k.spatialIn.x(), k.spatialIn.y()};
            if (!k.spatialOut.isNull()) ko["spatialOut"] = QJsonArray{k.spatialOut.x(), k.spatialOut.y()};
            keys.append(ko);
        }
        tr["keys"] = keys;
        o[it.key()] = tr;
    }
    return o;
}

static bool readValue(const QJsonValue& jv, const PropertyDef& d, PropValue& out, QString* err) {
    const int n = propComponents(d.type);
    if (!jv.isArray() || jv.toArray().size() != n) { if (err) *err = "property '" + d.id + "' has a value with the wrong number of components"; return false; }
    PropValue v;
    for (int i = 0; i < n; ++i) {
        const QJsonValue c = jv.toArray().at(i);
        if (!c.isDouble() || !std::isfinite(c.toDouble())) { if (err) *err = "property '" + d.id + "' has a non-numeric value"; return false; }
        v.c[i] = c.toDouble();
    }
    out = clampValue(d, v);
    return true;
}

static bool readHandle(const QJsonValue& jv, Handle& h, QString* err) {
    const QJsonArray a = jv.toArray();
    if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble() || !std::isfinite(a[0].toDouble()) || !std::isfinite(a[1].toDouble())) {
        if (err) *err = "malformed keyframe handle";
        return false;
    }
    h.x = std::clamp(a[0].toDouble(), 0.0, 1.0);
    h.y = std::clamp(a[1].toDouble(), -100.0, 100.0);
    return true;
}

bool tracksFromJson(const QJsonObject& o, PropertyTracks& out, QString* err, QStringList* warnings) {
    out.clear();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const PropertyDef* d = findPropertyDef(it.key());
        if (!d) { if (warnings) warnings->append("Ignored unknown animated property '" + it.key() + "'."); continue; }
        if (!it->isObject()) { if (err) *err = "property '" + d->id + "' is malformed"; return false; }
        const QJsonObject to = it->toObject();
        PropertyTrack tr;
        if (!readValue(to["base"], *d, tr.base, err)) return false;
        const QJsonArray keys = to["keys"].toArray();
        if (keys.size() > kMaxKeyframesPerProperty) { if (err) *err = "property '" + d->id + "' has too many keyframes"; return false; }
        for (const QJsonValue& kv : keys) {
            const QJsonObject ko = kv.toObject();
            Keyframe k;
            if (!ko["frame"].isDouble()) { if (err) *err = "keyframe without a frame number"; return false; }
            const double f = ko["frame"].toDouble();
            if (!std::isfinite(f) || f < 0 || f > 1e9 || f != std::floor(f)) { if (err) *err = "keyframe frame out of range"; return false; }
            k.frame = int(f);
            if (!readValue(ko["value"], *d, k.value, err)) return false;
            bool ok1 = true, ok2 = true;
            k.in = ko.contains("in") ? interpFromKey(ko["in"].toString(), &ok1) : Interp::Linear;
            k.out = ko.contains("out") ? interpFromKey(ko["out"].toString(), &ok2) : Interp::Linear;
            if (!ok1 || !ok2) { if (err) *err = "unknown keyframe interpolation"; return false; }
            if (ko.contains("inHandle") && !readHandle(ko["inHandle"], k.inHandle, err)) return false;
            if (ko.contains("outHandle") && !readHandle(ko["outHandle"], k.outHandle, err)) return false;
            for (const char* nm : {"spatialIn", "spatialOut"}) {
                if (!ko.contains(nm)) continue;
                const QJsonArray sa = ko[nm].toArray();
                if (sa.size() != 2 || !sa[0].isDouble() || !sa[1].isDouble() || !std::isfinite(sa[0].toDouble()) || !std::isfinite(sa[1].toDouble())) {
                    if (err) *err = "bad spatial tangent";
                    return false;
                }
                (QString(nm) == "spatialIn" ? k.spatialIn : k.spatialOut) = QPointF(sa[0].toDouble(), sa[1].toDouble());
            }
            tr.keys.push_back(k);
        }
        std::stable_sort(tr.keys.begin(), tr.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        for (int i = tr.keys.size() - 1; i > 0; --i) if (tr.keys[i].frame == tr.keys[i - 1].frame) tr.keys.remove(i - 1);   // later duplicate wins
        out.insert(d->id, tr);
    }
    pruneDefaults(out);
    return true;
}

}  // namespace mynd
