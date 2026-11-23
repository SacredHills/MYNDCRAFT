#include <QtTest>
#include <QPainter>

#include "TestEnv.h"
#include "Graphics/Compositor.h"

using namespace mynd;

namespace {
std::unique_ptr<Document> doc(QColor bg = Qt::white) {
    CompositionSettings s; s.width = 40; s.height = 30; s.background = bg;
    return std::make_unique<Document>(s);
}
QUuid solid(Document& d, const QString& name, QColor c, const QUuid& parent = {}) {
    const QUuid id = d.addLayer(LayerType::Raster, name, parent);
    d.findLayer(id)->raster.fill(c);
    return id;
}
QColor px(const QImage& i, int x, int y) { return i.pixelColor(x, y); }
bool near(const QColor& a, const QColor& b, int tol = 2) {
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol && qAbs(a.blue() - b.blue()) <= tol && qAbs(a.alpha() - b.alpha()) <= tol;
}
}  // namespace

class TestCompositor : public QObject {
    Q_OBJECT
private slots:
    void layersStackBottomToTop() {
        auto d = doc();
        solid(*d, "bottom", Qt::red);
        solid(*d, "top", Qt::blue);
        QCOMPARE(px(Compositor::render(*d), 5, 5), QColor(Qt::blue));
    }
    void opacityBlendsWithBackground() {
        auto d = doc(Qt::white);
        const QUuid id = solid(*d, "a", Qt::black);
        LayerProps p = d->findLayer(id)->props; p.opacity = 0.5; d->setLayerProps(id, p);
        QVERIFY2(near(px(Compositor::render(*d), 3, 3), QColor(128, 128, 128)), "50% black over white = mid gray");
    }
    void multiplyBlendMode() {
        auto d = doc(Qt::white);
        solid(*d, "base", QColor(255, 0, 0));
        const QUuid m = solid(*d, "mul", QColor(128, 128, 128));
        LayerProps p = d->findLayer(m)->props; p.blend = BlendMode::Multiply; d->setLayerProps(m, p);
        QVERIFY2(near(px(Compositor::render(*d), 1, 1), QColor(128, 0, 0)), "red * 50% gray");
    }
    void hiddenLayersAreSkipped() {
        auto d = doc();
        solid(*d, "a", Qt::red);
        const QUuid b = solid(*d, "b", Qt::blue);
        LayerProps p = d->findLayer(b)->props; p.visible = false; d->setLayerProps(b, p);
        QCOMPARE(px(Compositor::render(*d), 1, 1), QColor(Qt::red));
    }
    void transparentBackgroundExport() {
        auto d = doc(QColor(0, 0, 0, 0));
        QImage out = Compositor::render(*d);
        QCOMPARE(px(out, 2, 2).alpha(), 0);
        auto d2 = doc(Qt::white);
        QCOMPARE(px(Compositor::render(*d2, d2->canvasRect(), {false}), 2, 2).alpha(), 0);
    }
    void groupOpacityIsAppliedToTheGroupAsAUnit() {
        auto d = doc(Qt::white);
        const QUuid g = d->addLayer(LayerType::Group, "G");
        solid(*d, "r", Qt::black, g);          // two overlapping opaque layers inside the group
        solid(*d, "b", Qt::black, g);
        LayerProps p = d->findLayer(g)->props; p.opacity = 0.5; d->setLayerProps(g, p);
        // Isolated group: result is 50% black over white (not 75% from double-blending).
        QVERIFY2(near(px(Compositor::render(*d), 4, 4), QColor(128, 128, 128)), "group must be composited as a unit");
    }
    void vectorShapesRenderWithFillStrokeAndGradient() {
        auto d = doc(Qt::white);
        const QUuid v = d->addLayer(LayerType::Vector, "v");
        VectorShape r = VectorShape::makeRect(QRectF(5, 5, 20, 20));
        r.fill.color = QColor(0, 160, 0); r.stroke.enabled = false;
        VectorShape g = VectorShape::makeRect(QRectF(0, 0, 40, 4));
        g.stroke.enabled = false; g.fill.type = Paint::LinearGradient; g.fill.gradStart = {0, 0}; g.fill.gradEnd = {40, 0};
        g.fill.stops = {{0, Qt::black}, {1, Qt::white}};
        d->setShapes(v, {r, g}, "x");
        QImage out = Compositor::render(*d);
        QCOMPARE(px(out, 15, 15), QColor(0, 160, 0));
        QCOMPARE(px(out, 30, 28), QColor(Qt::white));
        QVERIFY(px(out, 2, 1).red() < 40);        // gradient dark end
        QVERIFY(px(out, 37, 1).red() > 215);      // gradient light end
        // Vector layers honour opacity and blend like raster layers.
        LayerProps p = d->findLayer(v)->props; p.opacity = 0.5; d->setLayerProps(v, p);
        QVERIFY(px(Compositor::render(*d), 15, 15).red() > 100);
    }
    void regionRenderMatchesFullRenderCrop() {
        auto d = doc(Qt::white);
        solid(*d, "a", QColor(10, 200, 30, 200));
        const QUuid v = d->addLayer(LayerType::Vector, "v");
        d->setShapes(v, {VectorShape::makeEllipse(QRectF(8, 4, 24, 20))}, "x");
        const QUuid m = solid(*d, "m", QColor(120, 40, 200, 160));
        LayerProps p = d->findLayer(m)->props; p.blend = BlendMode::Screen; p.opacity = 0.6; d->setLayerProps(m, p);
        const QImage full = Compositor::render(*d);
        const QRect r(7, 3, 20, 15);
        QCOMPARE(Compositor::render(*d, r, {}), full.copy(r));
    }

    void textLayerRendersAndHonorsOpacityAndVisibility() {
        auto d = doc(Qt::white);
        TextContent c; c.text = "MMMM"; c.sizePx = 24; c.color = Qt::black; c.pos = {2, 2};
        const QUuid id = d->addTextLayer(c, "t");
        auto dark = [](const QImage& i) { int n = 0; for (int y = 0; y < i.height(); ++y) for (int x = 0; x < i.width(); ++x) if (i.pixelColor(x, y).lightness() < 100) ++n; return n; };
        QVERIFY(dark(Compositor::render(*d)) > 40);
        LayerProps p = d->findLayer(id)->props;
        p.visible = false; d->setLayerProps(id, p);
        QCOMPARE(dark(Compositor::render(*d)), 0);
        p.visible = true; p.opacity = 0.5; d->setLayerProps(id, p);
        const QImage half = Compositor::render(*d);
        QCOMPARE(dark(half), 0);                                       // 50% black on white is mid grey, never dark
        bool anyGrey = false;
        for (int y = 0; y < half.height() && !anyGrey; ++y) for (int x = 0; x < half.width(); ++x) { const int l = half.pixelColor(x, y).lightness(); if (l > 100 && l < 180) { anyGrey = true; break; } }
        QVERIFY(anyGrey);
    }
};

QTEST_MAIN(TestCompositor)
#include "test_compositor.moc"
