#pragma once
#include <QImage>
#include <QPainter>
#include <QSet>

#include "Scene/Document.h"

namespace mynd {

class Renderer2D;

// Renders the layer tree of a Document to an image. Rendering is always expressed in document
// pixel coordinates; `region` selects the sub-rectangle to produce (used for incremental redraw).
class Compositor {
public:
    // Onion skin: faint tinted copies of neighbouring drawings under the current one (editor aid only;
    // export never enables it). Steps count distinct drawings unless includeHeld is set, in which
    // case every frame is a step.
    struct OnionSkin {
        bool enabled = false;
        int before = 2, after = 1;
        double opacity = 0.5;
        QColor beforeColor{255, 70, 70};
        QColor afterColor{60, 200, 90};
        bool includeHeld = false;
        bool activeLayerOnly = true;
        QUuid activeLayer;
    };
    struct Options {
        bool includeBackground = true;
        int frame = -1;          // -1 = the document's current frame
        OnionSkin onion;
        int scene3dSupersample = 0;   // 0 = automatic (2x anti-aliasing unless the canvas is large), 1 = fastest, 2 = anti-aliased (export)
    };
    struct Ctx { int frame; const OnionSkin* onion; const SymbolResolver* syms = nullptr; int depth = 0; int ss3d = 1; const QSet<QUuid>* cards = nullptr; };
    static QImage render(const Document& doc, const QRect& region, const Options& opt);
    // Depth pass of a frame: 16-bit grayscale, white = nearest, black = farthest or nothing 3D there. Made from every visible 3D scene layer
    // (the nearest opaque surface wins; 2D layers have no depth). The range of real distances (scene units) is returned in *nearOut/*farOut.
    static QImage renderDepth(const Document& doc, int frame, double* nearOut = nullptr, double* farOut = nullptr);
    static QImage render(const Document& doc) { return render(doc, doc.canvasRect(), Options{}); }

    // The 3D scene of `sceneLayer` at `frame` as it is drawn: animation applied and every card node showing the current content of its
    // 2D layer (rendered at `canvas` size, or larger when the card appears bigger than the canvas on screen, so it stays sharp; `supersample` is
    // the scene render's anti-aliasing factor). Same pointer as sceneLayer.scene3d when nothing is animated and there are no cards.
    static std::shared_ptr<const Scene3D> sceneForDrawing(const Layer& sceneLayer, int frame, const QSize& canvas, const SymbolResolver* syms = nullptr, int supersample = 1);
    // Layers (anywhere below `group`) that are shown through a card of some 3D scene layer and so are not drawn by themselves.
    static void collectCardSources(const Layer& group, QSet<QUuid>& out);

    static void drawShape(QPainter& p, const VectorShape& s);
    static void drawShapes(QPainter& p, const ShapeList& shapes);
    static QPainter::CompositionMode mode(BlendMode m);

private:
    static void drawGroup(QPainter& p, const Layer& group, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawLayer(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawScene3D(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawSymbol(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawAnimationLayer(QPainter& p, const Layer& l, const QRect& region, const Ctx& ctx, double opacity);
    static void drawLayerContent(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx,
                                 double opacity, BlendMode blend);
};

}  // namespace mynd
