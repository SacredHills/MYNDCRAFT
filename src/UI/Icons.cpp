#include "UI/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace mynd::icons {

namespace {
QIcon make(const std::function<void(QPainter&)>& draw) {
    QIcon icon;
    for (QColor c : {QColor(0xd6, 0xd6, 0xd6)}) {
        const int S = 48;
        QPixmap pm(S, S);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(1.0, 1.0);
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        draw(p);  // drawing space is 24x24
        icon.addPixmap(pm);
    }
    return icon;
}
}  // namespace

QIcon tool(ToolId id) {
    return make([id](QPainter& p) {
        const QColor c(0xd6, 0xd6, 0xd6);
        switch (id) {
        case ToolId::Select: {
            QPolygonF a{{6, 4}, {6, 19}, {10, 15}, {13, 21}, {15.5, 20}, {12.5, 14}, {18, 14}};
            p.setBrush(c); p.drawPolygon(a); break;
        }
        case ToolId::DirectSelect: {
            QPolygonF a{{6, 4}, {6, 19}, {10, 15}, {13, 21}, {15.5, 20}, {12.5, 14}, {18, 14}};
            p.drawPolygon(a); break;
        }
        case ToolId::Brush:
            p.setPen(QPen(c, 3.5, Qt::SolidLine, Qt::RoundCap)); p.drawLine(8, 16, 18, 5);
            p.setPen(QPen(c, 1.6)); p.setBrush(c); p.drawEllipse(QPointF(7, 17), 3.5, 3.5); break;
        case ToolId::Pencil:
            p.drawLine(5, 19, 17, 7); p.drawLine(7, 21, 19, 9); p.drawLine(17, 7, 19, 9);
            p.drawLine(5, 19, 4, 22); p.drawLine(4, 22, 7, 21); break;
        case ToolId::Eraser: {
            p.save(); p.translate(12, 12); p.rotate(-40);
            p.drawRect(QRectF(-8, -4, 16, 8)); p.drawLine(0, -4, 0, 4); p.restore();
            p.drawLine(4, 20, 20, 20); break;
        }
        case ToolId::Pen: {
            QPainterPath n; n.moveTo(12, 3); n.lineTo(18, 14); n.lineTo(12, 20); n.lineTo(6, 14); n.closeSubpath();
            p.drawPath(n); p.drawLine(12, 20, 12, 22); p.drawEllipse(QPointF(12, 12), 1.5, 1.5); break;
        }
        case ToolId::Line: p.drawLine(5, 19, 19, 5); break;
        case ToolId::Rectangle: p.drawRect(QRectF(4.5, 6.5, 15, 11)); break;
        case ToolId::Ellipse: p.drawEllipse(QPointF(12, 12), 8, 6); break;
        case ToolId::Polygon: {
            QPolygonF a;
            for (int i = 0; i < 6; ++i) a << QPointF(12 + 8 * std::cos(i * M_PI / 3), 12 + 8 * std::sin(i * M_PI / 3));
            p.drawPolygon(a); break;
        }
        case ToolId::Bucket: {
            p.save(); p.translate(11, 12); p.rotate(-20);
            p.drawRect(QRectF(-6, -5, 12, 11)); p.restore();
            p.setBrush(c); p.drawEllipse(QPointF(19, 17), 1.8, 2.6); break;
        }
        case ToolId::Gradient: {
            QLinearGradient g(4, 0, 20, 0); g.setColorAt(0, c); g.setColorAt(1, Qt::transparent);
            p.setBrush(g); p.drawRect(QRectF(4, 6, 16, 12)); break;
        }
        case ToolId::Eyedropper:
            p.drawLine(6, 18, 15, 9); p.drawLine(4, 20, 6, 18);
            p.setPen(QPen(c, 4, Qt::SolidLine, Qt::RoundCap)); p.drawLine(15, 8, 18, 5);
            p.setPen(QPen(c, 1.6)); p.drawLine(12, 6, 18, 12); break;
        case ToolId::Hand: {
            QPainterPath h; h.moveTo(8, 20); h.lineTo(5, 13); h.lineTo(7, 12); h.lineTo(9, 14); h.lineTo(9, 5);
            h.lineTo(11, 5); h.lineTo(11, 11); h.lineTo(13, 4.5); h.lineTo(15, 5); h.lineTo(14, 11); h.lineTo(17, 7);
            h.lineTo(19, 8); h.lineTo(16, 20); h.closeSubpath(); p.drawPath(h); break;
        }
        case ToolId::Zoom:
            p.drawEllipse(QPointF(10, 10), 6, 6); p.drawLine(14.5, 14.5, 20, 20);
            p.drawLine(7.5, 10, 12.5, 10); p.drawLine(10, 7.5, 10, 12.5); break;
        }
    });
}

QIcon layerType(LayerType t) {
    return make([t](QPainter& p) {
        const QColor c(0xd6, 0xd6, 0xd6);
        if (t == LayerType::Raster) {
            p.drawRect(QRectF(4.5, 4.5, 15, 15));
            p.setBrush(c); p.drawRect(QRectF(4.5, 4.5, 7, 7)); p.drawRect(QRectF(12, 12, 7, 7));
        } else if (t == LayerType::Vector) {
            QPainterPath b; b.moveTo(4, 18); b.cubicTo(8, 2, 16, 2, 20, 18); p.drawPath(b);
            p.setBrush(c); p.drawRect(QRectF(2.5, 16.5, 3, 3)); p.drawRect(QRectF(18.5, 16.5, 3, 3));
        } else {
            QPainterPath f; f.moveTo(3.5, 7); f.lineTo(9, 7); f.lineTo(11, 9); f.lineTo(20.5, 9);
            f.lineTo(20.5, 19.5); f.lineTo(3.5, 19.5); f.closeSubpath(); p.drawPath(f);
        }
    });
}

QIcon colorSwatch(const QColor& c, int s) {
    QPixmap pm(s, s);
    pm.fill(c);
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(0, 0, s - 1, s - 1);
    return QIcon(pm);
}

}  // namespace mynd::icons
