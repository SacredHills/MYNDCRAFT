#include "Scene3D/Anim3D.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace mynd {

static const QString kPrefix = QStringLiteral("3d:");

QString nodePropId(const QUuid& node, const QString& what) { return kPrefix + "n:" + node.toString(QUuid::WithoutBraces) + ":" + what; }
QString materialPropId(const QUuid& m, const QString& what) { return kPrefix + "m:" + m.toString(QUuid::WithoutBraces) + ":" + what; }
bool isScene3DPropId(const QString& id) { return id.startsWith(kPrefix); }

bool parseScene3DPropId(const QString& id, PropTarget* out) {
    // 3d:n:<36 chars>:<what>
    if (!id.startsWith(kPrefix) || id.size() < 3 + 2 + 36 + 1 + 1) return false;
    const QChar kind = id[3];
    if ((kind != 'n' && kind != 'm') || id[4] != ':' || id[5 + 36] != ':') return false;
    const QUuid u = QUuid::fromString(id.mid(5, 36));
    if (u.isNull()) return false;
    if (out) { out->material = kind == 'm'; out->target = u; out->what = id.mid(5 + 36 + 1); }
    return true;
}

static PropertyDef mk(const QString& id, const QString& label, const QString& group, PropType t, PropValue def, double lo, double hi,
                      QStringList names, const QString& unit, int dec) {
    PropertyDef d;
    d.id = id; d.label = label; d.group = group; d.type = t; d.def = def;
    for (int i = 0; i < 4; ++i) { d.minV.c[i] = lo; d.maxV.c[i] = hi; }
    d.componentNames = names; d.unit = unit; d.decimals = dec;
    return d;
}

static bool buildDef(const QString& id, const PropTarget& t, PropertyDef* out) {
    const QString& w = t.what;
    if (!t.material) {
        if (w == "pos") *out = mk(id, "Position", "3D Transform", PropType::Vec3, PropValue::of(0, 0, 0), -1e6, 1e6, {"X", "Y", "Z"}, "", 3);
        else if (w == "rot") *out = mk(id, "Rotation", "3D Transform", PropType::Vec3, PropValue::of(0, 0, 0), -1e6, 1e6, {"X", "Y", "Z"}, "°", 1);
        else if (w == "scl") *out = mk(id, "Scale", "3D Transform", PropType::Vec3, PropValue::of(1, 1, 1), -1e4, 1e4, {"X", "Y", "Z"}, "", 3);
        else if (w == "fov") *out = mk(id, "Field of View", "Camera", PropType::Float, PropValue::of(45), 1, 175, {"°"}, "°", 1);
        else if (w == "lint") *out = mk(id, "Intensity", "Light", PropType::Float, PropValue::of(1), 0, 1000, {""}, "", 2);
        else if (w == "lcol") { *out = mk(id, "Colour", "Light", PropType::Color, PropValue::of(255, 255, 255, 255), 0, 255, {"R", "G", "B", "A"}, "", 0); }
        else if (w == "lrange") *out = mk(id, "Range", "Light", PropType::Float, PropValue::of(0), 0, 1e6, {""}, "", 2);
        else if (w == "lspot") *out = mk(id, "Spot Angle", "Light", PropType::Float, PropValue::of(45), 1, 179, {"°"}, "°", 1);
        else return false;
    } else {
        if (w == "base") *out = mk(id, "Base Colour", "Material", PropType::Color, PropValue::of(200, 200, 200, 255), 0, 255, {"R", "G", "B", "A"}, "", 0);
        else if (w == "emis") *out = mk(id, "Emissive", "Material", PropType::Color, PropValue::of(0, 0, 0, 255), 0, 255, {"R", "G", "B", "A"}, "", 0);
        else if (w == "rough") *out = mk(id, "Roughness", "Material", PropType::Float, PropValue::of(0.6), 0, 1, {""}, "", 2);
        else if (w == "metal") *out = mk(id, "Metallic", "Material", PropType::Float, PropValue::of(0), 0, 1, {""}, "", 2);
        else return false;
    }
    return true;
}

const PropertyDef* scene3DPropertyDef(const QString& id) {
    static QMutex mu;
    static std::map<QString, std::unique_ptr<PropertyDef>> cache;
    PropTarget t;
    if (!parseScene3DPropId(id, &t)) return nullptr;
    QMutexLocker lock(&mu);
    auto it = cache.find(id);
    if (it != cache.end()) return it->second.get();
    auto d = std::make_unique<PropertyDef>();
    if (!buildDef(id, t, d.get())) return nullptr;
    return (cache[id] = std::move(d)).get();
}

QVector<QString> animatableProps(const Scene3D& s) {
    QVector<QString> v;
    for (const Node3D& n : s.nodes) {
        v << nodePropId(n.id, "pos") << nodePropId(n.id, "rot") << nodePropId(n.id, "scl");
        if (n.kind == NodeKind::Camera) v << nodePropId(n.id, "fov");
        if (n.kind == NodeKind::Light) {
            v << nodePropId(n.id, "lint") << nodePropId(n.id, "lcol");
            if (n.lightType != LightType::Directional) v << nodePropId(n.id, "lrange");
            if (n.lightType == LightType::Spot) v << nodePropId(n.id, "lspot");
        }
    }
    for (const Material& m : s.materials)
        v << materialPropId(m.id, "base") << materialPropId(m.id, "emis") << materialPropId(m.id, "rough") << materialPropId(m.id, "metal");
    return v;
}

bool scenePropExists(const Scene3D& s, const QString& id) { return readSceneProp(s, id, nullptr); }

static PropValue colorVal(const QColor& c) { return PropValue::of(c.red(), c.green(), c.blue(), c.alpha()); }
static QColor valColor(const PropValue& v) {
    auto q = [](double x) { return int(std::lround(std::clamp(x, 0.0, 255.0))); };
    return QColor(q(v.c[0]), q(v.c[1]), q(v.c[2]), q(v.c[3]));
}

bool readSceneProp(const Scene3D& s, const QString& id, PropValue* out) {
    PropTarget t;
    if (!parseScene3DPropId(id, &t)) return false;
    PropValue v;
    if (t.material) {
        const Material* m = s.material(t.target);
        if (!m) return false;
        if (t.what == "base") v = colorVal(m->baseColor);
        else if (t.what == "emis") v = colorVal(m->emissive);
        else if (t.what == "rough") v = PropValue::of(m->roughness);
        else if (t.what == "metal") v = PropValue::of(m->metallic);
        else return false;
    } else {
        const Node3D* n = s.node(t.target);
        if (!n) return false;
        if (t.what == "pos") v = PropValue::of(n->position.x(), n->position.y(), n->position.z());
        else if (t.what == "rot") v = PropValue::of(n->rotation.x(), n->rotation.y(), n->rotation.z());
        else if (t.what == "scl") v = PropValue::of(n->scale.x(), n->scale.y(), n->scale.z());
        else if (t.what == "fov" && n->kind == NodeKind::Camera) v = PropValue::of(n->fov);
        else if (n->kind == NodeKind::Light && t.what == "lint") v = PropValue::of(n->intensity);
        else if (n->kind == NodeKind::Light && t.what == "lcol") v = colorVal(n->lightColor);
        else if (n->kind == NodeKind::Light && t.what == "lrange") v = PropValue::of(n->range);
        else if (n->kind == NodeKind::Light && t.what == "lspot") v = PropValue::of(n->spotAngle);
        else return false;
    }
    if (out) *out = v;
    return true;
}

bool writeSceneProp(Scene3D& s, const QString& id, PropValue v) {
    PropTarget t;
    if (!parseScene3DPropId(id, &t)) return false;
    const PropertyDef* d = scene3DPropertyDef(id);
    if (!d) return false;
    v = clampValue(*d, v);
    if (t.material) {
        Material* m = s.material(t.target);
        if (!m) return false;
        if (t.what == "base") m->baseColor = valColor(v);
        else if (t.what == "emis") m->emissive = valColor(v);
        else if (t.what == "rough") m->roughness = v.c[0];
        else if (t.what == "metal") m->metallic = v.c[0];
        else return false;
    } else {
        Node3D* n = s.node(t.target);
        if (!n) return false;
        if (t.what == "pos") n->position = QVector3D(v.c[0], v.c[1], v.c[2]);
        else if (t.what == "rot") n->rotation = QVector3D(v.c[0], v.c[1], v.c[2]);
        else if (t.what == "scl") n->scale = QVector3D(v.c[0], v.c[1], v.c[2]);
        else if (t.what == "fov" && n->kind == NodeKind::Camera) n->fov = v.c[0];
        else if (n->kind == NodeKind::Light && t.what == "lint") n->intensity = v.c[0];
        else if (n->kind == NodeKind::Light && t.what == "lcol") n->lightColor = valColor(v);
        else if (n->kind == NodeKind::Light && t.what == "lrange") n->range = v.c[0];
        else if (n->kind == NodeKind::Light && t.what == "lspot") n->spotAngle = v.c[0];
        else return false;
    }
    return true;
}

QString describeSceneProp(const Scene3D& s, const QString& id) {
    PropTarget t;
    const PropertyDef* d = scene3DPropertyDef(id);
    if (!d || !parseScene3DPropId(id, &t)) return id;
    QString who;
    if (t.material) { const Material* m = s.material(t.target); who = m ? m->name : QObject::tr("(missing material)"); }
    else { const Node3D* n = s.node(t.target); who = n ? n->name : QObject::tr("(missing node)"); }
    return who + QStringLiteral(" › ") + d->label;
}

bool clipTimeAt(const ClipPlacement& p, const AnimClip& c, double frame, double* out) {
    if (!p.enabled || frame < p.start || c.length <= 0) return false;
    double t = (frame - p.start) * p.speed + p.offset;
    if (p.loop) { t = std::fmod(t, double(c.length)); if (t < 0) t += c.length; }
    else t = std::clamp(t, 0.0, double(c.length));
    if (out) *out = t;
    return true;
}

static bool placementHasTracks(const Scene3D& s, const ClipPlacement& p) {
    if (!p.enabled) return false;
    const AnimClip* c = s.clip(p.clip);
    return c && c->length > 0 && !c->tracks.isEmpty();
}

QSet<QString> drivenProps(const Scene3D& s, const PropertyTracks& t) {
    QSet<QString> v;
    for (const ClipPlacement& p : s.placements) {
        if (!placementHasTracks(s, p)) continue;
        const AnimClip* c = s.clip(p.clip);
        for (auto it = c->tracks.begin(); it != c->tracks.end(); ++it) if (it->animated() && scenePropExists(s, it.key())) v.insert(it.key());
    }
    for (auto it = t.begin(); it != t.end(); ++it)
        if (isScene3DPropId(it.key()) && it->animated() && scenePropExists(s, it.key())) v.insert(it.key());
    return v;
}

bool hasSceneAnimation(const Scene3D& s, const PropertyTracks& t) {
    for (const ClipPlacement& p : s.placements) if (placementHasTracks(s, p)) return true;
    for (auto it = t.begin(); it != t.end(); ++it)
        if (isScene3DPropId(it.key()) && it->animated() && scenePropExists(s, it.key())) return true;
    return false;
}

Scene3D evaluateScene(const Scene3D& base, const PropertyTracks& t, double frame) {
    if (!hasSceneAnimation(base, t)) return base;
    Scene3D s = base;
    quint64 h = 1469598103934665603ull;     // FNV-1a over the evaluated values
    auto mix = [&h](quint64 x) { h ^= x; h *= 1099511628211ull; };
    auto mixValue = [&](const QString& id, const PropValue& v) {
        mix(qHash(id));
        for (int i = 0; i < 4; ++i) { quint64 b; double x = v.c[i]; memcpy(&b, &x, sizeof b); mix(b); }
    };
    // Placed clips first (in order), then the layer's own keyframes override them.
    for (const ClipPlacement& p : base.placements) {
        if (!placementHasTracks(base, p)) continue;
        const AnimClip* c = base.clip(p.clip);
        double ct;
        if (!clipTimeAt(p, *c, frame, &ct)) continue;
        for (auto it = c->tracks.begin(); it != c->tracks.end(); ++it) {
            if (!it->animated()) continue;
            const PropertyDef* d = scene3DPropertyDef(it.key());
            if (!d || !scenePropExists(s, it.key())) continue;
            const PropValue v = it->valueAt(*d, ct);
            writeSceneProp(s, it.key(), v);
            mixValue(it.key(), v);
        }
    }
    for (auto it = t.begin(); it != t.end(); ++it) {
        if (!isScene3DPropId(it.key()) || !it->animated()) continue;
        const PropertyDef* d = scene3DPropertyDef(it.key());
        if (!d || !scenePropExists(s, it.key())) continue;
        const PropValue v = it->valueAt(*d, frame);
        writeSceneProp(s, it.key(), v);
        mixValue(it.key(), v);
    }
    s.revision = (base.revision * 0x9E3779B97F4A7C15ull) ^ h ^ (1ull << 63);
    return s;
}

PropertyTracks pruneSceneTracks(const Scene3D& s, const PropertyTracks& t) {
    PropertyTracks out;
    for (auto it = t.begin(); it != t.end(); ++it) {
        if (isScene3DPropId(it.key()) && !scenePropExists(s, it.key())) continue;
        out.insert(it.key(), it.value());
    }
    return out;
}

QVector<QString> animatedSceneProps(const Scene3D& s, const PropertyTracks& t) {
    QVector<QString> v;
    for (const QString& id : animatableProps(s)) {
        auto it = t.constFind(id);
        if (it != t.constEnd() && it->animated()) v << id;
    }
    return v;
}

}  // namespace mynd
