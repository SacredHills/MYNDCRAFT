#pragma once
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QThreadPool>

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
    struct Ctx { int frame; const OnionSkin* onion; const SymbolResolver* syms = nullptr; int depth = 0; int ss3d = 1; const QSet<QUuid>* cards = nullptr; const Layer* root = nullptr; int matteDepth = 0; };
    static QImage render(const Document& doc, const QRect& region, const Options& opt);
    // A frozen copy of what render() reads from a Document (layer tree, symbols, size, background, current frame; images shared copy-on-write),
    // so a render can run on a worker thread while the document keeps being edited.
    struct Snapshot : SymbolResolver {
        LayerPtr root;
        QVector<SymbolDef> symbols;
        QSize size;
        QColor background;
        int frame = 0;
        const SymbolDef* findSymbol(const QUuid& id) const override { for (const SymbolDef& d : symbols) if (d.id == id) return &d; return nullptr; }
        QRect rect() const { return QRect(QPoint(0, 0), size); }
    };
    // Pool for whole-picture renders that run in the background (canvas, 3D viewport). Separate from the global pool because those renders
    // themselves split work over the global pool; running them there as well could leave every thread waiting for another.
    static QThreadPool* backgroundPool();
    static int gpuEffectsUsed();      // how many layer-effect chains have been run on the GPU so far (diagnostics, tests)
    static std::shared_ptr<const Snapshot> snapshot(const Document& doc);
    static QImage render(const Snapshot& snap, const QRect& region, const Options& opt);
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
    static void drawLayerImpl(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx, bool ignoreHidden);
    static void drawLayerWithFx(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx, bool forCard = false);   // forCard: opacity and blend are left to the card's material
    static void drawScene3D(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawSymbol(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx);
    static void drawAnimationLayer(QPainter& p, const Layer& l, const QRect& region, const Ctx& ctx, double opacity);
    static void drawLayerContent(QPainter& p, const Layer& l, const QRect& region, const QSize& canvas, const Ctx& ctx,
                                 double opacity, BlendMode blend);
};

}  // namespace mynd
