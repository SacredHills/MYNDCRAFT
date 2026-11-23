#pragma once
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace mynd {

enum class TextAlign { Left = 0, Center = 1, Right = 2 };

// Content of a text layer. Stays live text (font, size, tracking ...) — never flattened to pixels.
struct TextContent {
    QString text;
    QString family;            // empty = application default font
    double sizePx = 48;
    bool bold = false, italic = false;
    QColor color = Qt::black;
    double tracking = 0;       // extra letter spacing in px
    TextAlign align = TextAlign::Left;
    QPointF pos;               // top-left corner of the text box (document space)
    double boxWidth = 0;       // 0 = point text (no wrapping); >0 = wrapped paragraph width

    QFont font() const;
    QRectF bounds() const;     // document-space bounds of the laid-out text
    void draw(QPainter& p) const;
    bool isEmpty() const { return text.trimmed().isEmpty(); }

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& o, TextContent& out, QString* err);
    bool operator==(const TextContent& o) const;
};

}  // namespace mynd
