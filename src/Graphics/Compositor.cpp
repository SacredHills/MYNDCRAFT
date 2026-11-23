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
    drawGroup(p, *doc.root(), r, doc.canvasSize());
    return out;
}

void Compositor::drawGroup(QPainter& p, const Layer& group, const QRect& region, const QSize& canvas) {
    for (const auto& c : group.children) drawLayer(p, *c, region, canvas);
}

void Compositor::drawLayer(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas) {
    if (!l.props.visible || l.props.opacity <= 0) return;
    const bool plain = l.props.opacity >= 1.0 && l.props.blend == BlendMode::Normal;

    p.save();
    if (l.type == LayerType::Raster) {
        if (!l.raster.isNull()) {
            p.setOpacity(l.props.opacity);
            p.setCompositionMode(mode(l.props.blend));
            p.drawImage(region.topLeft(), l.raster, region);
        }
    } else if (plain) {
        // Fast path: no isolation needed.
        if (l.type == LayerType::Vector) drawShapes(p, l.shapes);
        else if (l.type == LayerType::Text) l.textContent.draw(p);
        else drawGroup(p, l, region, canvas);
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
        else drawGroup(bp, l, region, canvas);
        bp.end();
        p.setOpacity(l.props.opacity);
        p.setCompositionMode(mode(l.props.blend));
        p.drawImage(region.topLeft(), buf);
    }
    p.restore();
}

}  // namespace mynd
