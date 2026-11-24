#pragma once
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>

namespace mynd {

// Draws the check boxes of item views (layer list, 3D hierarchy) as a light box with a dark tick, so they stand out on the dark theme.
class LightCheckStyle : public QProxyStyle {
public:
    LightCheckStyle() = default;
    void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
        if (pe != PE_IndicatorItemViewItemCheck) { QProxyStyle::drawPrimitive(pe, opt, p, w); return; }
        const int side = qMin(qMin(opt->rect.width(), opt->rect.height()), 14);
        const QRectF r(opt->rect.center().x() - side / 2.0 + 0.5, opt->rect.center().y() - side / 2.0 + 0.5, side - 1, side - 1);
        const bool on = opt->state & State_On, enabled = opt->state & State_Enabled;
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(QColor(150, 150, 150), 1));
        p->setBrush(enabled ? QColor(232, 232, 232) : QColor(150, 150, 150));
        p->drawRoundedRect(r, 2, 2);
        if (on) {
            p->setPen(QPen(QColor(30, 30, 30), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPolyline(QPolygonF({QPointF(r.left() + r.width() * 0.22, r.top() + r.height() * 0.55),
                                       QPointF(r.left() + r.width() * 0.43, r.top() + r.height() * 0.75),
                                       QPointF(r.left() + r.width() * 0.80, r.top() + r.height() * 0.28)}));
        } else if (opt->state & State_NoChange) {
            p->fillRect(QRectF(r.left() + 3, r.center().y() - 1, r.width() - 6, 2), QColor(30, 30, 30));
        }
        p->restore();
    }
};

}  // namespace mynd
