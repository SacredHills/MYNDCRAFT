#pragma once
#include <QImage>
#include <QPainter>

#include "Scene/Document.h"

namespace mynd {

class Renderer2D;

// Renders the layer tree of a Document to an image. Rendering is always expressed in document
// pixel coordinates; `region` selects the sub-rectangle to produce (used for incremental redraw).
class Compositor {
public:
    struct Options {
        bool includeBackground = true;
    };
    static QImage render(const Document& doc, const QRect& region, const Options& opt);
    static QImage render(const Document& doc) { return render(doc, doc.canvasRect(), Options{}); }

    static void drawShape(QPainter& p, const VectorShape& s);
    static void drawShapes(QPainter& p, const ShapeList& shapes);
    static QPainter::CompositionMode mode(BlendMode m);

private:
    static void drawGroup(QPainter& p, const Layer& group, const QRect& region, const QSize& canvas);
    static void drawLayer(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas);
};

}  // namespace mynd
