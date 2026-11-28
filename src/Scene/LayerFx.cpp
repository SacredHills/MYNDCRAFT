#include "Scene/LayerFx.h"

#include <QJsonArray>
#include <map>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace mynd {

static const EffectInfo kInfo[] = {
    {EffectType::Blur, "blur", "Gaussian Blur", 1, {{"Radius (px)", 0, 300, 8}}},
    {EffectType::DropShadow, "dropShadow", "Drop Shadow", 4, {{"Offset X (px)", -500, 500, 6}, {"Offset Y (px)", -500, 500, 6}, {"Blur (px)", 0, 300, 6}, {"Opacity (%)", 0, 100, 60}}},
    {EffectType::Glow, "glow", "Glow", 3, {{"Radius (px)", 0, 300, 12}, {"Intensity (%)", 0, 400, 100}, {"Threshold (%)", 0, 100, 60}}},
    {EffectType::BrightnessContrast, "brightnessContrast", "Brightness & Contrast", 2, {{"Brightness", -100, 100, 0}, {"Contrast", -100, 100, 0}}},
    {EffectType::HueSaturation, "hueSaturation", "Hue / Saturation", 3, {{"Hue (deg)", -180, 180, 0}, {"Saturation", -100, 100, 0}, {"Lightness", -100, 100, 0}}},
    {EffectType::Invert, "invert", "Invert Colors", 0, {}},
};
const EffectInfo& effectInfo(EffectType t) { for (const auto& i : kInfo) if (i.type == t) return i; return kInfo[0]; }
const QVector<EffectType>& allEffectTypes() {
    static const QVector<EffectType> v = [] { QVector<EffectType> r; for (const auto& i : kInfo) r << i.type; return r; }();
    return v;
}
bool effectTypeFromKey(const QString& key, EffectType& out) {
    for (const auto& i : kInfo) if (key == i.key) { out = i.type; return true; }
    return false;
}

Effect Effect::make(EffectType t) {
    Effect e; e.type = t;
    const EffectInfo& i = effectInfo(t);
    for (int k = 0; k < i.paramCount; ++k) e.p[k] = i.params[k].def;
    return e;
}
bool Effect::operator==(const Effect& o) const {
    return type == o.type && enabled == o.enabled && color == o.color && p[0] == o.p[0] && p[1] == o.p[1] && p[2] == o.p[2] && p[3] == o.p[3];
}
QJsonObject Effect::toJson() const {
    QJsonObject o; o["id"] = id.toString(QUuid::WithoutBraces); o["type"] = effectInfo(type).key; o["enabled"] = enabled;
    QJsonArray a; for (int k = 0; k < effectInfo(type).paramCount; ++k) a.append(p[k]);
    o["params"] = a; o["color"] = color.name();
    return o;
}
bool Effect::fromJson(const QJsonObject& o, Effect& out, QString* err) {
    EffectType t;
    if (!effectTypeFromKey(o["type"].toString(), t)) { if (err) *err = "unknown effect '" + o["type"].toString() + "'"; return false; }
    out = Effect::make(t);
    if (const QUuid u(o["id"].toString()); !u.isNull()) out.id = u;
    out.enabled = o["enabled"].toBool(true);
    const EffectInfo& i = effectInfo(t);
    const QJsonArray a = o["params"].toArray();
    for (int k = 0; k < i.paramCount && k < a.size(); ++k) {
        const double v = a[k].toDouble(i.params[k].def);
        out.p[k] = std::isfinite(v) ? std::clamp(v, i.params[k].min, i.params[k].max) : i.params[k].def;
    }
    const QColor c(o["color"].toString());
    if (c.isValid()) out.color = c;
    return true;
}

QJsonObject Mask::toJson() const {
    QJsonObject o; o["id"] = id.toString(QUuid::WithoutBraces); o["name"] = name; o["shape"] = shape.toJson();
    o["mode"] = mode == MaskMode::Add ? "add" : mode == MaskMode::Subtract ? "subtract" : "intersect";
    o["feather"] = feather; o["opacity"] = opacity; o["invert"] = invert;
    return o;
}
bool Mask::fromJson(const QJsonObject& o, Mask& out, QString* err) {
    if (!VectorShape::fromJson(o["shape"].toObject(), out.shape, err)) return false;
    if (const QUuid u(o["id"].toString()); !u.isNull()) out.id = u;
    out.name = o["name"].toString("Mask");
    const QString m = o["mode"].toString("add");
    out.mode = m == "subtract" ? MaskMode::Subtract : m == "intersect" ? MaskMode::Intersect : MaskMode::Add;
    const double f = o["feather"].toDouble(0), op = o["opacity"].toDouble(100);
    out.feather = std::isfinite(f) ? std::clamp(f, 0.0, 500.0) : 0.0;
    out.opacity = std::isfinite(op) ? std::clamp(op, 0.0, 100.0) : 100.0;
    out.invert = o["invert"].toBool(false);
    return true;
}

QJsonObject LayerFx::toJson() const {
    QJsonObject o;
    QJsonArray m; for (const Mask& k : masks) m.append(k.toJson());
    QJsonArray e; for (const Effect& k : effects) e.append(k.toJson());
    o["masks"] = m; o["effects"] = e;
    if (!matteLayer.isNull() && matteMode != MatteMode::None) {
        o["matteLayer"] = matteLayer.toString(QUuid::WithoutBraces);
        static const char* keys[] = {"none", "alpha", "invertedAlpha", "luma", "invertedLuma"};
        o["matteMode"] = keys[int(matteMode)];
    }
    return o;
}
LayerFx LayerFx::fromJson(const QJsonObject& o, QStringList* warnings) {
    LayerFx f;
    for (const auto& v : o["masks"].toArray()) { Mask m; QString err; if (Mask::fromJson(v.toObject(), m, &err)) f.masks << m; else if (warnings) *warnings << "mask skipped: " + err; }
    for (const auto& v : o["effects"].toArray()) { Effect e; QString err; if (Effect::fromJson(v.toObject(), e, &err)) f.effects << e; else if (warnings) *warnings << "effect skipped: " + err; }
    const QUuid id(o["matteLayer"].toString());
    const QString mm = o["matteMode"].toString();
    static const char* keys[] = {"none", "alpha", "invertedAlpha", "luma", "invertedLuma"};
    for (int i = 1; i < 5; ++i) if (mm == keys[i] && !id.isNull()) { f.matteMode = MatteMode(i); f.matteLayer = id; }
    return f;
}

// ---------------------------------------------------------------- keyframing
bool isFxPropId(const QString& id) { return id.startsWith(QLatin1String("fx:")); }
QString fxEffectPropId(const Effect& e, int k) { return QString("fx:%1:%2:%3").arg(effectInfo(e.type).key, e.id.toString(QUuid::WithoutBraces)).arg(k); }
QString fxMaskPropId(const Mask& m, const QString& what) { return QString("fx:mask:%1:%2").arg(m.id.toString(QUuid::WithoutBraces), what); }

const PropertyDef* fxPropertyDef(const QString& id) {
    static std::mutex mu;
    static std::map<QString, PropertyDef> cache;
    std::lock_guard<std::mutex> lock(mu);
    if (auto it = cache.find(id); it != cache.end()) return &it->second;
    const QStringList parts = id.split(':');
    if (parts.size() != 4 || parts[0] != "fx" || QUuid(parts[2]).isNull()) return nullptr;
    PropertyDef d; d.id = id; d.group = "Effects"; d.type = PropType::Float; d.decimals = 1;
    double lo = 0, hi = 0, def = 0;
    if (parts[1] == "mask") {
        if (parts[3] == "feather") { d.label = "Mask Feather"; d.unit = "px"; lo = 0; hi = 500; def = 0; }
        else if (parts[3] == "opacity") { d.label = "Mask Opacity"; d.unit = "%"; lo = 0; hi = 100; def = 100; d.decimals = 0; }
        else return nullptr;
    } else {
        EffectType t;
        bool ok = false;
        const int k = parts[3].toInt(&ok);
        if (!effectTypeFromKey(parts[1], t) || !ok) return nullptr;
        const EffectInfo& info = effectInfo(t);
        if (k < 0 || k >= info.paramCount) return nullptr;
        d.label = QString("%1 › %2").arg(info.label, info.params[k].label);
        lo = info.params[k].min; hi = info.params[k].max; def = info.params[k].def;
    }
    d.def = PropValue::of(def);
    for (int i = 0; i < 4; ++i) { d.minV.c[i] = lo; d.maxV.c[i] = hi; }
    d.componentNames = {QString()};
    return &cache.emplace(id, d).first->second;
}

QVector<QString> fxPropertyIds(const LayerFx& fx, const PropertyTracks* animatedIn) {
    QVector<QString> ids;
    auto add = [&](const QString& id) {
        if (animatedIn) { auto it = animatedIn->constFind(id); if (it == animatedIn->constEnd() || !it->animated()) return; }
        ids << id;
    };
    for (const Effect& e : fx.effects) for (int k = 0; k < effectInfo(e.type).paramCount; ++k) add(fxEffectPropId(e, k));
    for (const Mask& m : fx.masks) { add(fxMaskPropId(m, "feather")); add(fxMaskPropId(m, "opacity")); }
    return ids;
}

LayerFx fxAtFrame(const LayerFx& fx, const PropertyTracks& tracks, double frame) {
    if (tracks.isEmpty() || (fx.effects.isEmpty() && fx.masks.isEmpty())) return fx;
    LayerFx out = fx;
    auto value = [&](const QString& id, double* v) {
        auto it = tracks.constFind(id);
        if (it == tracks.constEnd() || !it->animated()) return;
        if (const PropertyDef* d = fxPropertyDef(id)) *v = it->valueAt(*d, frame).c[0];
    };
    for (Effect& e : out.effects) for (int k = 0; k < effectInfo(e.type).paramCount; ++k) value(fxEffectPropId(e, k), &e.p[k]);
    for (Mask& m : out.masks) { value(fxMaskPropId(m, "feather"), &m.feather); value(fxMaskPropId(m, "opacity"), &m.opacity); }
    return out;
}

bool readFxValue(const LayerFx& fx, const QString& id, double* out) {
    const QStringList parts = id.split(':');
    if (parts.size() != 4 || parts[0] != "fx") return false;
    const QUuid u(parts[2]);
    if (parts[1] == "mask") {
        for (const Mask& m : fx.masks) if (m.id == u) { if (parts[3] == "feather") { *out = m.feather; return true; } if (parts[3] == "opacity") { *out = m.opacity; return true; } }
        return false;
    }
    const int k = parts[3].toInt();
    for (const Effect& e : fx.effects) if (e.id == u && k >= 0 && k < effectInfo(e.type).paramCount) { *out = e.p[k]; return true; }
    return false;
}
bool writeFxValue(LayerFx& fx, const QString& id, double v) {
    const QStringList parts = id.split(':');
    if (parts.size() != 4 || parts[0] != "fx") return false;
    const QUuid u(parts[2]);
    if (parts[1] == "mask") {
        for (Mask& m : fx.masks) if (m.id == u) { if (parts[3] == "feather") { m.feather = v; return true; } if (parts[3] == "opacity") { m.opacity = v; return true; } }
        return false;
    }
    const int k = parts[3].toInt();
    for (Effect& e : fx.effects) if (e.id == u && k >= 0 && k < effectInfo(e.type).paramCount) { e.p[k] = v; return true; }
    return false;
}

}  // namespace mynd
