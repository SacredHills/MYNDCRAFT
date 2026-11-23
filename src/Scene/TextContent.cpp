#include "Scene/TextContent.h"

#include <QFontMetricsF>
#include <cmath>

namespace mynd {

QFont TextContent::font() const {
    QFont f = family.isEmpty() ? QFont() : QFont(family);
    f.setPixelSize(std::max(1, int(std::lround(sizePx))));
    f.setBold(bold);
    f.setItalic(italic);
    if (tracking != 0) f.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    return f;
}

namespace {
int flagsFor(const TextContent& t) {
    int f = Qt::TextDontClip;
    if (t.boxWidth > 0) f |= Qt::TextWordWrap;
    switch (t.align) {
    case TextAlign::Left: f |= Qt::AlignLeft; break;
    case TextAlign::Center: f |= Qt::AlignHCenter; break;
    case TextAlign::Right: f |= Qt::AlignRight; break;
    }
    return f | Qt::AlignTop;
}
// Size of the text block; point text is as wide as its widest line.
QSizeF measure(const TextContent& t) {
    QFontMetricsF fm(t.font());
    const double w = t.boxWidth > 0 ? t.boxWidth : 100000.0;
    const QRectF r = fm.boundingRect(QRectF(0, 0, w, 100000.0), (flagsFor(t) & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft, t.text);
    return {t.boxWidth > 0 ? t.boxWidth : std::ceil(r.width()) + 2, std::ceil(r.height())};
}
}  // namespace

QRectF TextContent::bounds() const { return QRectF(pos, measure(*this)); }

void TextContent::draw(QPainter& p) const {
    if (text.isEmpty()) return;
    p.save();
    p.setFont(font());
    p.setPen(color);
    p.setBrush(Qt::NoBrush);
    p.drawText(bounds(), flagsFor(*this), text);
    p.restore();
}

bool TextContent::operator==(const TextContent& o) const {
    return text == o.text && family == o.family && sizePx == o.sizePx && bold == o.bold && italic == o.italic &&
           color == o.color && tracking == o.tracking && align == o.align && pos == o.pos && boxWidth == o.boxWidth;
}

QJsonObject TextContent::toJson() const {
    return QJsonObject{{"text", text}, {"family", family}, {"size", sizePx}, {"bold", bold}, {"italic", italic},
                       {"color", color.name(QColor::HexArgb)}, {"tracking", tracking}, {"align", int(align)},
                       {"x", pos.x()}, {"y", pos.y()}, {"boxWidth", boxWidth}};
}

bool TextContent::fromJson(const QJsonObject& o, TextContent& out, QString* err) {
    TextContent t;
    t.text = o["text"].toString();
    t.family = o["family"].toString();
    t.sizePx = o["size"].toDouble(48);
    if (!(t.sizePx >= 1 && t.sizePx <= 10000)) { if (err) *err = "text size out of range"; return false; }
    t.bold = o["bold"].toBool();
    t.italic = o["italic"].toBool();
    const QColor c(o["color"].toString());
    t.color = c.isValid() ? c : QColor(Qt::black);
    t.tracking = o["tracking"].toDouble();
    const int a = o["align"].toInt();
    t.align = a == 1 ? TextAlign::Center : a == 2 ? TextAlign::Right : TextAlign::Left;
    t.pos = QPointF(o["x"].toDouble(), o["y"].toDouble());
    t.boxWidth = std::max(0.0, o["boxWidth"].toDouble());
    out = t;
    return true;
}

}  // namespace mynd
