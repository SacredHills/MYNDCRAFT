#include <limits>
#include <cmath>
#include "Graphics/Compositor.h"
#include "Graphics/GpuEffects.h"
#include <atomic>
#include "Graphics/Effects.h"

#include <mutex>
#include <vector>

#include "Scene3D/Render3D.h"

namespace mynd {

QPainter::CompositionMode Compositor::mode(BlendMode m) {
    switch (m) {
    case BlendMode::Normal: return QPainter::CompositionMode_SourceOver;
    case BlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case BlendMode::Screen: return QPainter::CompositionMode_Screen;
    case BlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case BlendMode::Darken: return QPainter::CompositionMode_Darken;
    case BlendMode::Lighten: return QPainter::CompositionMode_Lighten;
    case BlendMode::Add: return QPainter::CompositionMode_Plus;
    case BlendMode::Difference: return QPainter::CompositionMode_Difference;
    }
    return QPainter::CompositionMode_SourceOver;
}

void Compositor::drawShape(QPainter& p, const VectorShape& s) {
    if (s.nodes.isEmpty()) return;
    p.save();
    p.setPen(s.stroke.toPen());
    p.setBrush(s.closed ? s.fill.toBrush() : QBrush(Qt::NoBrush));
    p.drawPath(s.path());
    p.restore();
}

void Compositor::drawShapes(QPainter& p, const ShapeList& shapes) {
    for (const auto& s : shapes) drawShape(p, s);
}

static QImage renderTree(const Layer& root, const SymbolResolver* syms, const QSize& size, const QColor& background, int currentFrame,
                         const QRect& region, const Compositor::Options& opt,
                         void (*drawGroupFn)(QPainter&, const Layer&, const QRect&, const QSize&, const Compositor::Ctx&)) {
    const QRect r = region.intersected(QRect(QPoint(0, 0), size));
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (r.isEmpty()) return out;
    out.fill(opt.includeBackground ? background : QColor(Qt::transparent));
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-r.topLeft());
    p.setClipRect(r);
    QSet<QUuid> cards;
    Compositor::collectCardSources(root, cards);
    const Compositor::Ctx ctx{opt.frame >= 0 ? opt.frame : currentFrame, &opt.onion, syms, 0, opt.scene3dSupersample, &cards, &root, 0};
    drawGroupFn(p, root, r, size, ctx);
    return out;
}

QImage Compositor::render(const Document& doc, const QRect& region, const Options& opt) {
    return renderTree(*doc.root(), &doc, doc.canvasSize(), doc.comp().background, doc.currentFrame(), region, opt, &Compositor::drawGroup);
}

static std::atomic<int> g_gpuUsed{0};
int Compositor::gpuEffectsUsed() { return g_gpuUsed; }

QThreadPool* Compositor::backgroundPool() {
    static QThreadPool* pool = [] { auto* p = new QThreadPool; p->setMaxThreadCount(2); p->setThreadPriority(QThread::LowPriority); return p; }();
    return pool;
}

std::shared_ptr<const Compositor::Snapshot> Compositor::snapshot(const Document& doc) {
    auto s = std::make_shared<Snapshot>();
    s->root = doc.root()->cloneExact(false);
    for (const SymbolDef& d : doc.symbols()) { SymbolDef c = d; if (d.root) c.root = d.root->cloneExact(false); s->symbols << c; }
    s->size = doc.canvasSize();
    s->background = doc.comp().background;
    s->frame = doc.currentFrame();
    return s;
}

QImage Compositor::render(const Snapshot& snap, const QRect& region, const Options& opt) {
    return renderTree(*snap.root, &snap, snap.size, snap.background, snap.frame, region, opt, &Compositor::drawGroup);
}

static void collectVisibleScenes(const Layer& g, std::vector<const Layer*>& out) {
    for (const auto& c : g.children) {
        if (!c->props.visible) continue;
        if (c->type == LayerType::Scene3D && c->scene3d) out.push_back(c.get());
        if (c->isGroup()) collectVisibleScenes(*c, out);
    }
}

QImage Compositor::renderDepth(const Document& doc, int frame, double* nearOut, double* farOut) {
    const QSize size = doc.canvasSize();
    QImage out(size, QImage::Format_Grayscale16);
    out.fill(0);
    if (size.isEmpty()) return out;
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> best(size_t(size.width()) * size.height(), inf);
    std::vector<const Layer*> scenes;
    collectVisibleScenes(*doc.root(), scenes);
    for (const Layer* l : scenes) {
        const auto scene = sceneForDrawing(*l, frame, size, &doc, 1);
        std::vector<float> d;
        RenderOptions ro; ro.supersample = 1; ro.drawBackground = false; ro.distanceOut = &d;
        renderScene(*scene, sceneCamera(*scene, double(size.width()) / size.height()), size, ro);
        for (size_t i = 0; i < best.size() && i < d.size(); ++i) best[i] = std::min(best[i], d[i]);
    }
    float lo = inf, hi = 0;
    for (float v : best) if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (nearOut) *nearOut = std::isfinite(lo) ? lo : 0;
    if (farOut) *farOut = std::isfinite(lo) ? hi : 0;
    if (!std::isfinite(lo)) return out;
    const float span = std::max(hi - lo, 1e-6f);
    for (int y = 0; y < size.height(); ++y) {
        quint16* row = reinterpret_cast<quint16*>(out.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const float v = best[size_t(y) * size.width() + x];
            row[x] = std::isfinite(v) ? quint16(std::lround(65535.0 * (hi - lo < 1e-6f ? 1.0 : 1.0 - double((v - lo) / span)) * 0.9 + 65535.0 * 0.1)) : 0;   // nearest = white, farthest surface = 10% grey, empty = black
        }
    }
    return out;
}

void Compositor::collectCardSources(const Layer& group, QSet<QUuid>& out) {
    for (const auto& c : group.children) {
        if (c->type == LayerType::Scene3D && c->scene3d)
            for (const Node3D& n : c->scene3d->nodes) if (!n.cardLayer.isNull()) out.insert(n.cardLayer);
        if (c->fx.matteMode != MatteMode::None && !c->fx.matteLayer.isNull()) out.insert(c->fx.matteLayer);     // a track matte is only seen through the layer it mattes
        if (c->isGroup()) collectCardSources(*c, out);
    }
}

static const Layer* findInTree(const Layer& g, const QUuid& id) {
    for (const auto& c : g.children) {
        if (c->id == id) return c.get();
        if (c->isGroup()) if (const Layer* f = findInTree(*c, id)) return f;
    }
    return nullptr;
}

std::shared_ptr<const Scene3D> Compositor::sceneForDrawing(const Layer& l, int frame, const QSize& canvas, const SymbolResolver* syms, int supersample) {
    std::shared_ptr<const Scene3D> base = l.sceneAt(frame);
    if (!base) return base;
    bool anyCard = false;
    for (const Node3D& n : base->nodes) anyCard |= !n.cardLayer.isNull();
    if (!anyCard || canvas.isEmpty()) return base;
    const Layer* root = &l;
    while (root->parent) root = root->parent;
    auto s = std::make_shared<Scene3D>(*base);
    const RenderCamera cam = sceneCamera(*base, double(canvas.width()) / canvas.height());
    // How many times larger than the canvas a card appears on screen (width or height, times the render's supersampling): the card's
    // picture is rendered that much bigger (0.5 steps, at most 4x and about 16 megapixels) so it stays sharp when scaled up or approached.
    auto textureScale = [&](const Node3D& n) {
        auto mit = base->meshes.find(n.mesh);
        if (mit == base->meshes.end()) return 1.0;
        QVector3D a, b; mit->second->bounds(&a, &b);
        const QMatrix4x4 world = base->worldMatrix(n.id);
        const QVector3D c[4] = {{a.x(), a.y(), a.z()}, {b.x(), a.y(), a.z()}, {b.x(), b.y(), a.z()}, {a.x(), b.y(), a.z()}};
        QPointF q[4];
        for (int i = 0; i < 4; ++i) if (!projectToScreen(cam, world.map(c[i]), canvas, &q[i])) return 1.0 * std::max(1, supersample);
        auto len = [](QPointF u, QPointF v) { return QLineF(u, v).length(); };
        const double w = std::max(len(q[0], q[1]), len(q[3], q[2])), hh = std::max(len(q[0], q[3]), len(q[1], q[2]));
        double need = std::max(w / canvas.width(), hh / canvas.height()) * std::max(1, supersample);
        if (!std::isfinite(need)) need = 1;
        double sc = std::clamp(std::ceil(need * 2) / 2, 1.0, 4.0);
        while (sc > 1.0 && double(canvas.width()) * sc * canvas.height() * sc > 16e6) sc -= 0.5;
        return sc;
    };
    quint64 h = 1469598103934665603ull;
    const QSet<QUuid> noCards;                                   // the layer shown on a card is drawn by the card only
    const Ctx ctx{frame, nullptr, syms, 1, 1, &noCards, root, 0};
    for (Node3D& n : s->nodes) {
        if (n.cardLayer.isNull()) continue;
        const Layer* src = findInTree(*root, n.cardLayer);
        if (!src || src == &l || src->type == LayerType::Scene3D || !src->props.visible) { n.visible = false; continue; }
        const double texScale = textureScale(n);
        QImage img(QSize(std::max(1, int(std::lround(canvas.width() * texScale))), std::max(1, int(std::lround(canvas.height() * texScale)))), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.scale(double(img.width()) / canvas.width(), double(img.height()) / canvas.height());
            // The layer's own 2D transform (anchor / position / scale / rotation, animated or not) is part of what the card shows, so a
            // layer keeps its 2D animation when it becomes a card; the card's 3D transform comes on top of it.
            const LayerXform xf = src->xformAt(frame);
            QRect region(QPoint(0, 0), canvas);
            if (src->fx.any()) {
                // effects, masks and a track matte are part of the layer's picture (opacity and blend stay with the card's material)
                drawLayerWithFx(p, *src, region, canvas, ctx, true);
                region = QRect();
            } else if (!xf.isIdentity()) {
                const QTransform m = xf.matrix(QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
                bool ok = false;
                const QTransform inv = m.inverted(&ok);
                if (!ok) region = QRect();                             // zero scale: nothing to show
                else {
                    region = inv.mapRect(QRectF(region)).toAlignedRect().adjusted(-2, -2, 2, 2).intersected(region);
                    p.setWorldTransform(m, true);
                }
            }
            if (!region.isEmpty()) drawLayerContent(p, *src, region, canvas, ctx, 1.0, BlendMode::Normal);
        }
        const double opacity = std::clamp(src->props.opacity * src->xformAt(frame).opacity, 0.0, 1.0);
        h ^= qHash(QByteArray::fromRawData(reinterpret_cast<const char*>(img.constBits()), qsizetype(img.sizeInBytes())), 0x9E3779B9u);
        h *= 1099511628211ull;
        h ^= quint64(opacity * 1e6) + quint64(n.cardLit);
        h *= 1099511628211ull;
        const QUuid tid = QUuid::createUuid();
        s->textures[tid] = img;
        Material m;
        m.id = QUuid::createUuid(); m.name = "Card"; m.baseColor = QColor::fromRgbF(1, 1, 1, float(opacity)); m.baseTexture = tid;
        m.roughness = 1.0; m.doubleSided = true; m.unlit = !n.cardLit;
        m.blendMode = int(src->props.blend);                  // the layer's blend mode, applied against what the scene has drawn behind the card
        n.materialOverride = m.id;
        s->materials.push_back(m);
    }
    s->revision = (base->revision * 0x9E3779B97F4A7C15ull) ^ h ^ (1ull << 62);
    return s;
}

void Compositor::drawGroup(QPainter& p, const Layer& group, const QRect& region, const QSize& canvas, const Ctx& ctx) {
    for (const auto& c : group.children) drawLayer(p, *c, region, canvas, ctx);
}

static QImage tinted(const QImage& src, const QRect& region, const QColor& color) {
    QImage t = src.copy(region);
    QPainter tp(&t);
    tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tp.fillRect(t.rect(), color);
    return t;
}

// Drawings to show as onion skin around `frame`, nearest first. Blank frames are skipped.
static QVector<QUuid> onionDrawings(const AnimationState& a, int frame, int steps, int dir, bool includeHeld) {
    QVector<QUuid> out;
    if (steps <= 0) return out;
    const QUuid current = a.drawingAt(frame);
    QUuid last = current;
    const int limit = dir < 0 ? 0 : a.endFrame() - 1;
    for (int f = frame + dir; (dir < 0 ? f >= limit : f <= limit) && out.size() < steps; f += dir) {
        const QUuid d = a.drawingAt(f);
        if (d.isNull()) continue;
        if (!includeHeld && d == last) continue;       // same drawing held over several frames = one step
        out.append(d);
        last = d;
    }
    return out;
}

void Compositor::drawSymbol(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx) {
    const SymbolDef* d = ctx.syms ? ctx.syms->findSymbol(l.symbolId) : nullptr;
    if (!d || !d->root || ctx.depth >= 8) return;       // missing symbol, or runaway nesting from a damaged file
    Ctx inner = ctx;
    inner.frame = symbolLocalFrame(l, *d, ctx.frame);
    inner.onion = nullptr;                              // onion skin is a per-timeline editing aid
    inner.depth = ctx.depth + 1;
    QSet<QUuid> innerCards;
    collectCardSources(*d->root, innerCards);
    inner.cards = &innerCards;
    drawGroup(p, *d->root, region, canvas, inner);
}

void Compositor::drawAnimationLayer(QPainter& p, const Layer& l, const QRect& region, const Ctx& ctx, double opacity) {
    const OnionSkin* on = ctx.onion;
    const bool onion = on && on->enabled && (!on->activeLayerOnly || on->activeLayer == l.id);
    if (!onion) {
        if (const QImage* img = l.imageAt(ctx.frame)) {
            p.setOpacity(opacity);
            p.setCompositionMode(mode(l.props.blend));
            p.drawImage(region.topLeft(), *img, region);
        }
        return;
    }
    QImage buf(region.size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    QPainter bp(&buf);
    auto drawOnion = [&](const QVector<QUuid>& ids, const QColor& color, int steps) {
        for (int i = ids.size() - 1; i >= 0; --i) {      // farthest first so nearer ones sit on top
            auto it = l.drawingImages.find(ids[i]);
            if (it == l.drawingImages.end()) continue;
            bp.setOpacity(on->opacity * double(steps - i) / steps);
            bp.drawImage(0, 0, tinted(it->second, region, color));
        }
    };
    drawOnion(onionDrawings(l.anim, ctx.frame, on->before, -1, on->includeHeld), on->beforeColor, std::max(1, on->before));
    drawOnion(onionDrawings(l.anim, ctx.frame, on->after, +1, on->includeHeld), on->afterColor, std::max(1, on->after));
    bp.setOpacity(1.0);
    if (const QImage* img = l.imageAt(ctx.frame)) bp.drawImage(0, 0, img->copy(region));
    bp.end();
    p.setOpacity(opacity);
    p.setCompositionMode(mode(l.props.blend));
    p.drawImage(region.topLeft(), buf);
}

void Compositor::drawScene3D(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx) {
    if (!l.scene3d || canvas.isEmpty()) return;
    int ss = ctx.ss3d;
    if (ss <= 0) ss = (qint64(canvas.width()) * canvas.height() <= 1200000) ? 2 : 1;
    const std::shared_ptr<const Scene3D> scene = sceneForDrawing(l, ctx.frame, canvas, ctx.syms, ss);
    // The software renderer draws the whole canvas, so keep the last few results: repaints of other layers, partial redraws and
    // scrubbing over frames where the scene did not change are free.
    struct Entry { quint64 rev; QSize size; int ss; QImage img; };
    static std::mutex mu;
    static std::vector<Entry> cache;
    QImage img;
    {
        std::lock_guard<std::mutex> lock(mu);
        for (const Entry& e : cache) if (e.rev == scene->revision && e.size == canvas && e.ss == ss) { img = e.img; break; }
    }
    if (img.isNull()) {
        RenderOptions ro;
        ro.supersample = ss;
        img = renderScene(*scene, sceneCamera(*scene, double(canvas.width()) / canvas.height()), canvas, ro);
        std::lock_guard<std::mutex> lock(mu);
        cache.insert(cache.begin(), Entry{scene->revision, canvas, ss, img});
        if (cache.size() > 4) cache.pop_back();
    }
    p.drawImage(region.topLeft(), img, region);
}

// Draws the layer's own content (no transform) into `p`, restricted to `region`, with `opacity` and `blend`.
void Compositor::drawLayerContent(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx,
                                  double opacity, BlendMode blend) {
    const bool plain = opacity >= 1.0 && blend == BlendMode::Normal;
    p.save();
    if (l.type == LayerType::Raster) {
        if (!l.raster.isNull()) {
            p.setOpacity(opacity);
            p.setCompositionMode(mode(blend));
            p.drawImage(region.topLeft(), l.raster, region);
        }
    } else if (l.type == LayerType::Animation) {
        drawAnimationLayer(p, l, region, ctx, opacity);
    } else if (plain) {
        // Fast path: no isolation needed.
        if (l.type == LayerType::Vector) drawShapes(p, l.shapesAt(ctx.frame));
        else if (l.type == LayerType::Text) l.textContent.draw(p);
        else if (l.type == LayerType::Symbol) drawSymbol(p, l, region, canvas, ctx);
        else if (l.type == LayerType::Scene3D) drawScene3D(p, l, region, canvas, ctx);
        else drawGroup(p, l, region, canvas, ctx);
    } else {
        // Isolated: render to a transparent buffer sized to the region, then blend as a unit.
        QImage buf(region.size(), QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        QPainter bp(&buf);
        bp.setRenderHint(QPainter::Antialiasing);
        bp.translate(-region.topLeft());
        bp.setClipRect(region);
        if (l.type == LayerType::Vector) drawShapes(bp, l.shapesAt(ctx.frame));
        else if (l.type == LayerType::Text) l.textContent.draw(bp);
        else if (l.type == LayerType::Symbol) drawSymbol(bp, l, region, canvas, ctx);
        else if (l.type == LayerType::Scene3D) drawScene3D(bp, l, region, canvas, ctx);
        else drawGroup(bp, l, region, canvas, ctx);
        bp.end();
        p.setOpacity(opacity);
        p.setCompositionMode(mode(blend));
        p.drawImage(region.topLeft(), buf);
    }
    p.restore();
}

void Compositor::drawLayer(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx) {
    drawLayerImpl(p, l, region, canvas, ctx, false);
}

// Layer with effects, masks and/or a track matte: content -> effects -> masks (layer space) -> layer transform -> matte -> opacity/blend.
void Compositor::drawLayerWithFx(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx, bool forCard) {
    const LayerXform xf = l.xformAt(ctx.frame);
    const double opacity = forCard ? 1.0 : l.props.opacity * xf.opacity;
    if (opacity <= 0) return;
    const QTransform m = xf.matrix(QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
    bool invertible = false;
    const QTransform inv = m.inverted(&invertible);
    if (!invertible) return;
    const LayerFx lfx = l.fxAt(ctx.frame);                    // keyframed numbers at this frame
    const int margin = fx::effectMargin(lfx.effects);
    const QRect limit = QRect(QPoint(0, 0), canvas).adjusted(-margin, -margin, margin, margin);
    const QRect src = inv.mapRect(QRectF(region)).toAlignedRect().adjusted(-2 - margin, -2 - margin, 2 + margin, 2 + margin).intersected(limit);
    if (src.isEmpty()) return;
    QImage buf(src.size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    {
        QPainter bp(&buf);
        bp.setRenderHint(QPainter::Antialiasing);
        bp.translate(-src.topLeft());
        bp.setClipRect(src);
        drawLayerContent(bp, l, src, canvas, ctx, 1.0, BlendMode::Normal);
    }
    if (!lfx.effects.isEmpty()) {
        if (fx::gpu::enabled() && fx::gpu::applyEffects(buf, lfx.effects)) ++g_gpuUsed;
        else fx::applyEffects(buf, lfx.effects);
    }
    if (!lfx.masks.isEmpty()) fx::applyCoverage(buf, fx::maskCoverage(lfx.masks, src.size(), src.topLeft()));
    QImage comp(region.size(), QImage::Format_ARGB32_Premultiplied);
    comp.fill(Qt::transparent);
    {
        QPainter cp(&comp);
        cp.setRenderHint(QPainter::SmoothPixmapTransform);
        cp.translate(-region.topLeft());
        cp.setWorldTransform(m, true);
        cp.drawImage(src.topLeft(), buf);
    }
    if (l.fx.matteMode != MatteMode::None && !l.fx.matteLayer.isNull() && ctx.root && ctx.matteDepth < 8) {
        if (const Layer* ml = findInTree(*ctx.root, l.fx.matteLayer); ml && ml != &l) {
            QImage matte(region.size(), QImage::Format_ARGB32_Premultiplied);
            matte.fill(Qt::transparent);
            {
                QPainter mp(&matte);
                mp.setRenderHint(QPainter::Antialiasing);
                mp.translate(-region.topLeft());
                Ctx mc = ctx; mc.matteDepth = ctx.matteDepth + 1;
                drawLayerImpl(mp, *ml, region, canvas, mc, true);
            }
            fx::applyMatte(comp, matte, l.fx.matteMode);
        }
    }
    p.save();
    p.setOpacity(opacity);
    p.setCompositionMode(forCard ? QPainter::CompositionMode_SourceOver : mode(l.props.blend));
    p.drawImage(region.topLeft(), comp);
    p.restore();
}

void Compositor::drawLayerImpl(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx, bool ignoreHidden) {
    if (!l.props.visible && !ignoreHidden) return;
    if (!ignoreHidden && ctx.cards && ctx.cards->contains(l.id)) return;           // shown through a card in a 3D scene instead (or used as a track matte)
    if (l.fx.any()) { drawLayerWithFx(p, l, region, canvas, ctx); return; }
    const LayerXform xf = l.xformAt(ctx.frame);
    const double opacity = l.props.opacity * xf.opacity;
    if (opacity <= 0) return;
    if (xf.isIdentity()) { drawLayerContent(p, l, region, canvas, ctx, opacity, l.props.blend); return; }

    // Transformed layer: render its content unrotated into a buffer covering just the part of the canvas that
    // can land inside `region`, then draw that buffer through the layer matrix.
    const QTransform m = xf.matrix(QPointF(canvas.width() / 2.0, canvas.height() / 2.0));
    bool invertible = false;
    const QTransform inv = m.inverted(&invertible);
    if (!invertible) return;                                   // zero scale: nothing visible
    const QRect src = inv.mapRect(QRectF(region)).toAlignedRect().adjusted(-2, -2, 2, 2).intersected(QRect(QPoint(0, 0), canvas));
    if (src.isEmpty()) return;
    QImage buf(src.size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    QPainter bp(&buf);
    bp.setRenderHint(QPainter::Antialiasing);
    bp.translate(-src.topLeft());
    bp.setClipRect(src);
    drawLayerContent(bp, l, src, canvas, ctx, 1.0, BlendMode::Normal);
    bp.end();
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setWorldTransform(m, true);
    p.setOpacity(opacity);
    p.setCompositionMode(mode(l.props.blend));
    p.drawImage(src.topLeft(), buf);
    p.restore();
}

}  // namespace mynd
