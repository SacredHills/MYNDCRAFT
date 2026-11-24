#include "Scene/Layer.h"

#include <algorithm>

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
    case LayerType::Text: return "text";
    case LayerType::Animation: return "animation";
    }
    return "raster";
}
bool layerTypeFromKey(const QString& k, LayerType& out) {
    if (k == "raster") out = LayerType::Raster;
    else if (k == "vector") out = LayerType::Vector;
    else if (k == "group") out = LayerType::Group;
    else if (k == "text") out = LayerType::Text;
    else if (k == "animation") out = LayerType::Animation;
    else return false;
    return true;
}

Layer::Layer(LayerType t, const QString& name) : id(QUuid::createUuid()), type(t) { props.name = name; }

QImage* Layer::drawingImage(const QUuid& id) {
    auto it = drawingImages.find(id);
    return it == drawingImages.end() ? nullptr : &it->second;
}
const QImage* Layer::imageAt(int frame) const {
    if (type == LayerType::Raster) return raster.isNull() ? nullptr : &raster;
    if (type != LayerType::Animation) return nullptr;
    const QUuid d = anim.drawingAt(frame);
    if (d.isNull()) return nullptr;
    auto it = drawingImages.find(d);
    return it == drawingImages.end() || it->second.isNull() ? nullptr : &it->second;
}
QImage* Layer::imageForEdit(int frame) { return const_cast<QImage*>(static_cast<const Layer*>(this)->imageAt(frame)); }

static QRect alphaBounds(const QImage& img) {
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    const QImage a = img.format() == QImage::Format_ARGB32_Premultiplied || img.format() == QImage::Format_ARGB32 ? img : img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto* row = reinterpret_cast<const quint32*>(a.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (row[x] >> 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    }
    return x1 < 0 ? QRect() : QRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

QRectF Layer::contentBounds(int frame, const QSize& canvas) const {
    switch (type) {
    case LayerType::Raster:
    case LayerType::Animation: {
        const QImage* img = imageAt(frame);
        return img ? QRectF(alphaBounds(*img)) : QRectF();
    }
    case LayerType::Vector: {
        QRectF r;
        for (const auto& s : shapes) r = r.united(s.bounds());
        return r;
    }
    case LayerType::Text: return textContent.bounds();
    case LayerType::Group: {
        QRectF r;
        const QPointF c(canvas.width() / 2.0, canvas.height() / 2.0);
        for (const auto& ch : children) {
            if (!ch->props.visible) continue;
            const QRectF b = ch->contentBounds(frame, canvas);
            if (b.isNull()) continue;
            r = r.united(ch->xformAt(frame).matrix(c).mapRect(b));
        }
        return r;
    }
    }
    return {};
}

LayerPtr Layer::cloneExact() const {
    auto c = std::make_shared<Layer>(type, props.name);
    c->id = id;
    c->props = props;
    c->raster = raster.copy();
    c->shapes = shapes;
    c->textContent = textContent;
    c->anim = anim;
    c->tracks = tracks;
    for (const auto& [did, img] : drawingImages) c->drawingImages[did] = img.copy();
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
    c->textContent = textContent;
    c->anim = anim;
    c->tracks = tracks;
    for (const auto& [did, img] : drawingImages) c->drawingImages[did] = img.copy();
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
