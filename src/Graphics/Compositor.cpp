#include "Graphics/Compositor.h"

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

QImage Compositor::render(const Document& doc, const QRect& region, const Options& opt) {
    const QRect r = region.intersected(doc.canvasRect());
    QImage out(r.size(), QImage::Format_ARGB32_Premultiplied);
    if (r.isEmpty()) return out;
    out.fill(opt.includeBackground ? doc.comp().background : QColor(Qt::transparent));
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-r.topLeft());
    p.setClipRect(r);
    const Ctx ctx{opt.frame >= 0 ? opt.frame : doc.currentFrame(), &opt.onion};
    drawGroup(p, *doc.root(), r, doc.canvasSize(), ctx);
    return out;
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

void Compositor::drawAnimationLayer(QPainter& p, const Layer& l, const QRect& region, const Ctx& ctx) {
    const OnionSkin* on = ctx.onion;
    const bool onion = on && on->enabled && (!on->activeLayerOnly || on->activeLayer == l.id);
    if (!onion) {
        if (const QImage* img = l.imageAt(ctx.frame)) {
            p.setOpacity(l.props.opacity);
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
    p.setOpacity(l.props.opacity);
    p.setCompositionMode(mode(l.props.blend));
    p.drawImage(region.topLeft(), buf);
}

void Compositor::drawLayer(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx) {
    if (!l.props.visible || l.props.opacity <= 0) return;
    const bool plain = l.props.opacity >= 1.0 && l.props.blend == BlendMode::Normal;

    p.save();
    if (l.type == LayerType::Raster) {
        if (!l.raster.isNull()) {
            p.setOpacity(l.props.opacity);
            p.setCompositionMode(mode(l.props.blend));
            p.drawImage(region.topLeft(), l.raster, region);
        }
    } else if (l.type == LayerType::Animation) {
        drawAnimationLayer(p, l, region, ctx);
    } else if (plain) {
        // Fast path: no isolation needed.
        if (l.type == LayerType::Vector) drawShapes(p, l.shapes);
        else if (l.type == LayerType::Text) l.textContent.draw(p);
        else drawGroup(p, l, region, canvas, ctx);
    } else {
        // Isolated: render to a transparent buffer sized to the region, then blend as a unit.
        QImage buf(region.size(), QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        QPainter bp(&buf);
        bp.setRenderHint(QPainter::Antialiasing);
        bp.translate(-region.topLeft());
        bp.setClipRect(region);
        if (l.type == LayerType::Vector) drawShapes(bp, l.shapes);
        else if (l.type == LayerType::Text) l.textContent.draw(bp);
        else drawGroup(bp, l, region, canvas, ctx);
        bp.end();
        p.setOpacity(l.props.opacity);
        p.setCompositionMode(mode(l.props.blend));
        p.drawImage(region.topLeft(), buf);
    }
    p.restore();
}

}  // namespace mynd
