#include "Scene/Layer.h"
#include "Scene3D/Anim3D.h"

#include <algorithm>

#include "Scene/ShapeMorph.h"

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
    case LayerType::Symbol: return "symbol";
    case LayerType::Scene3D: return "scene3d";
    }
    return "raster";
}
bool layerTypeFromKey(const QString& k, LayerType& out) {
    if (k == "raster") out = LayerType::Raster;
    else if (k == "vector") out = LayerType::Vector;
    else if (k == "group") out = LayerType::Group;
    else if (k == "text") out = LayerType::Text;
    else if (k == "animation") out = LayerType::Animation;
    else if (k == "symbol") out = LayerType::Symbol;
    else if (k == "scene3d") out = LayerType::Scene3D;
    else return false;
    return true;
}

QString symbolKindKey(SymbolKind k) { return k == SymbolKind::MovieClip ? "movieclip" : "graphic"; }
bool symbolKindFromKey(const QString& s, SymbolKind& out) {
    if (s == "graphic") out = SymbolKind::Graphic; else if (s == "movieclip") out = SymbolKind::MovieClip; else return false;
    return true;
}
QString symbolPlayKey(SymbolPlay p) { return p == SymbolPlay::PlayOnce ? "once" : p == SymbolPlay::SingleFrame ? "single" : "loop"; }
bool symbolPlayFromKey(const QString& s, SymbolPlay& out) {
    if (s == "loop") out = SymbolPlay::Loop; else if (s == "once") out = SymbolPlay::PlayOnce; else if (s == "single") out = SymbolPlay::SingleFrame; else return false;
    return true;
}

int symbolLocalFrame(const Layer& inst, const SymbolDef& def, int parentFrame) {
    const int n = std::max(1, def.durationFrames);
    if (def.kind == SymbolKind::MovieClip) return ((parentFrame % n) + n) % n;     // free-running loop
    const int first = std::clamp(inst.symbolFirstFrame, 0, n - 1);
    switch (inst.symbolPlay) {
    case SymbolPlay::SingleFrame: return first;
    case SymbolPlay::PlayOnce: return std::min(first + std::max(0, parentFrame), n - 1);
    case SymbolPlay::Loop: break;
    }
    return ((first + parentFrame) % n + n) % n;
}

Layer::Layer(LayerType t, const QString& name) : id(QUuid::createUuid()), type(t) {
    props.name = name;
    if (t == LayerType::Scene3D) scene3d = std::make_shared<mynd::Scene3D>();
}

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

static QRectF childrenBounds(const std::vector<LayerPtr>& kids, int frame, const QSize& canvas, const SymbolResolver* syms) {
    QRectF r;
    const QPointF c(canvas.width() / 2.0, canvas.height() / 2.0);
    for (const auto& ch : kids) {
        if (!ch->props.visible) continue;
        const QRectF b = ch->contentBounds(frame, canvas, syms);
        if (b.isNull()) continue;
        r = r.united(ch->xformAt(frame).matrix(c).mapRect(b));
    }
    return r;
}

ShapeList Layer::shapesAt(double frame) const {
    if (!hasShapeTween() || editingEnd) return shapes;
    const double m = valueOf(tracks, *findPropertyDef("shape.morph"), frame).c[0] / 100.0;
    if (m <= 0) return shapes;
    if (m >= 1) return morphShapes;
    return mynd::morphShapes(shapes, morphShapes, m);
}

QRectF Layer::contentBounds(int frame, const QSize& canvas, const SymbolResolver* syms) const {
    switch (type) {
    case LayerType::Raster:
    case LayerType::Animation: {
        const QImage* img = imageAt(frame);
        return img ? QRectF(alphaBounds(*img)) : QRectF();
    }
    case LayerType::Vector: {
        QRectF r;
        for (const auto& s : shapesAt(frame)) r = r.united(s.bounds());
        return r;
    }
    case LayerType::Text: return textContent.bounds();
    case LayerType::Scene3D: return QRectF(QPointF(0, 0), QSizeF(canvas));
    case LayerType::Group: return childrenBounds(children, frame, canvas, syms);
    case LayerType::Symbol: {
        const SymbolDef* d = syms ? syms->findSymbol(symbolId) : nullptr;
        if (!d || !d->root) return {};
        static thread_local int depth = 0;           // symbols may not contain themselves, but a damaged file might
        if (depth > 8) return {};
        ++depth;
        const QRectF r = childrenBounds(d->root->children, symbolLocalFrame(*this, *d, frame), canvas, syms);
        --depth;
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
    c->morphShapes = morphShapes; c->editingEnd = editingEnd;
    c->textContent = textContent;
    c->anim = anim;
    c->tracks = tracks;
    c->fx = fx;
    c->symbolId = symbolId; c->symbolPlay = symbolPlay; c->symbolFirstFrame = symbolFirstFrame;
    if (scene3d) c->scene3d = std::make_shared<mynd::Scene3D>(*scene3d);
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
    c->morphShapes = morphShapes; c->editingEnd = editingEnd;
    c->textContent = textContent;
    c->anim = anim;
    c->tracks = tracks;
    c->fx = fx;
    c->symbolId = symbolId; c->symbolPlay = symbolPlay; c->symbolFirstFrame = symbolFirstFrame;
    if (scene3d) {
        c->scene3d = std::make_shared<mynd::Scene3D>(*scene3d);
        std::map<QUuid, QUuid> nodeMap;
        for (auto& n : c->scene3d->nodes) { const QUuid nid = QUuid::createUuid(); nodeMap[n.id] = nid; n.id = nid; }
        for (auto& n : c->scene3d->nodes) if (nodeMap.count(n.parent)) n.parent = nodeMap[n.parent];
        if (nodeMap.count(c->scene3d->activeCamera)) c->scene3d->activeCamera = nodeMap[c->scene3d->activeCamera];
        for (AnimClip& ac : c->scene3d->clips) {
            PropertyTracks rt;
            for (auto it = ac.tracks.begin(); it != ac.tracks.end(); ++it) {
                mynd::PropTarget pt;
                if (mynd::parseScene3DPropId(it.key(), &pt) && !pt.material && nodeMap.count(pt.target)) rt.insert(mynd::nodePropId(nodeMap[pt.target], pt.what), it.value());
            }
            ac.tracks = rt;
        }
        for (auto& k : c->scene3d->skins) for (QUuid& j : k.joints) if (nodeMap.count(j)) j = nodeMap[j];
        // 3D keyframe tracks follow their nodes (materials keep their ids: they are per-scene).
        PropertyTracks remapped;
        for (auto it = tracks.begin(); it != tracks.end(); ++it) {
            mynd::PropTarget pt;
            if (mynd::parseScene3DPropId(it.key(), &pt) && !pt.material) {
                auto m = nodeMap.find(pt.target);
                if (m == nodeMap.end()) continue;
                remapped.insert(mynd::nodePropId(m->second, pt.what), it.value());
            } else remapped.insert(it.key(), it.value());
        }
        c->tracks = remapped;
    }
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

namespace mynd {
QVector<QString> Layer::animated3DProperties() const {
    return scene3d ? animatedSceneProps(*scene3d, tracks) : QVector<QString>();
}
QString Layer::propertyLabel(const QString& id) const {
    if (isScene3DPropId(id)) return scene3d ? describeSceneProp(*scene3d, id) : id;
    const PropertyDef* d = findPropertyDef(id);
    return d ? d->label : id;
}
std::shared_ptr<const Scene3D> Layer::sceneAt(double frame) const {
    if (!scene3d) return nullptr;
    if (!hasSceneAnimation(*scene3d, tracks)) return scene3d;
    return std::make_shared<const Scene3D>(evaluateScene(*scene3d, tracks, frame));
}
}  // namespace mynd
