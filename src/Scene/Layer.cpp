#include "Scene/Layer.h"

namespace mynd {

namespace {
struct BM { BlendMode m; const char* key; const char* name; };
const BM kBlend[] = {
    {BlendMode::Normal, "normal", "Normal"},     {BlendMode::Multiply, "multiply", "Multiply"},
    {BlendMode::Screen, "screen", "Screen"},     {BlendMode::Overlay, "overlay", "Overlay"},
    {BlendMode::Darken, "darken", "Darken"},     {BlendMode::Lighten, "lighten", "Lighten"},
    {BlendMode::Add, "add", "Add"},              {BlendMode::Difference, "difference", "Difference"},
};
}  // namespace

QString blendModeName(BlendMode m) {
    for (const auto& b : kBlend) if (b.m == m) return b.name;
    return "Normal";
}
QString blendModeKey(BlendMode m) {
    for (const auto& b : kBlend) if (b.m == m) return b.key;
    return "normal";
}
BlendMode blendModeFromKey(const QString& k, bool* ok) {
    for (const auto& b : kBlend) if (k == b.key) { if (ok) *ok = true; return b.m; }
    if (ok) *ok = false;
    return BlendMode::Normal;
}
const std::vector<BlendMode>& allBlendModes() {
    static const std::vector<BlendMode> v = [] {
        std::vector<BlendMode> r;
        for (const auto& b : kBlend) r.push_back(b.m);
        return r;
    }();
    return v;
}
QString layerTypeKey(LayerType t) {
    switch (t) {
    case LayerType::Raster: return "raster";
    case LayerType::Vector: return "vector";
    case LayerType::Group: return "group";
    }
    return "raster";
}
bool layerTypeFromKey(const QString& k, LayerType& out) {
    if (k == "raster") out = LayerType::Raster;
    else if (k == "vector") out = LayerType::Vector;
    else if (k == "group") out = LayerType::Group;
    else return false;
    return true;
}

Layer::Layer(LayerType t, const QString& name) : id(QUuid::createUuid()), type(t) { props.name = name; }

LayerPtr Layer::cloneExact() const {
    auto c = std::make_shared<Layer>(type, props.name);
    c->id = id;
    c->props = props;
    c->raster = raster.copy();
    c->shapes = shapes;
    for (const auto& ch : children) {
        auto cc = ch->cloneExact();
        cc->parent = c.get();
        c->children.push_back(cc);
    }
    return c;
}

LayerPtr Layer::cloneWithNewIds() const {
    auto c = std::make_shared<Layer>(type, props.name);
    c->props = props;
    c->raster = raster.copy();
    c->shapes = shapes;
    for (auto& s : c->shapes) s.id = QUuid::createUuid();  // group ids stay consistent within the layer
    QHash<QUuid, QUuid> gmap;
    for (auto& s : c->shapes) {
        if (s.groupId.isNull()) continue;
        if (!gmap.contains(s.groupId)) gmap[s.groupId] = QUuid::createUuid();
        s.groupId = gmap[s.groupId];
    }
    for (const auto& ch : children) {
        auto cc = ch->cloneWithNewIds();
        cc->parent = c.get();
        c->children.push_back(cc);
    }
    return c;
}

}  // namespace mynd
