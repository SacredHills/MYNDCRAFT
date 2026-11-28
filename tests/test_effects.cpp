#include <QtTest>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Graphics/Effects.h"
#include "Scene/Document.h"

using namespace mynd;

// Phase 9: masks, track mattes and effects.
class TestEffects : public QObject {
    Q_OBJECT
    struct S { std::unique_ptr<Document> doc; QUuid a, b; };
    // 100x100 canvas (transparent background). Layer A: a solid red square covering everything. Layer B (on top): a white circle in the middle.
    static S build() {
        CompositionSettings cs; cs.width = 100; cs.height = 100; cs.durationFrames = 12; cs.background = Qt::transparent;
        S s; s.doc = std::make_unique<Document>(cs);
        s.a = s.doc->addLayer(LayerType::Vector, "A");
        VectorShape r = VectorShape::makeRect(QRectF(0, 0, 100, 100)); r.fill.color = Qt::red; r.stroke.enabled = false;
        s.doc->setShapes(s.a, {r}, "A");
        s.b = s.doc->addLayer(LayerType::Vector, "B");
        VectorShape c = VectorShape::makeEllipse(QRectF(30, 30, 40, 40)); c.fill.color = Qt::white; c.stroke.enabled = false;
        s.doc->setShapes(s.b, {c}, "B");
        s.doc->undoStack()->clear();
        return s;
    }
    static QImage render(const Document& d, int frame = 0) { Compositor::Options o; o.frame = frame; o.includeBackground = false; return Compositor::render(d, d.canvasRect(), o); }
    static Mask rectMask(QRectF r, MaskMode m = MaskMode::Add) {
        Mask k; k.shape = VectorShape::makeRect(r); k.shape.closed = true; k.mode = m; return k;
    }
    static void show(Document& d, const QUuid& id, bool v) { LayerProps p = d.findLayer(id)->props; p.visible = v; d.setLayerProps(id, p); }
    static bool isRed(const QColor& c) { return c.alpha() > 240 && c.red() > 240 && c.green() < 15 && c.blue() < 15; }

private slots:
    void aMaskCutsTheLayerAndFeatherSoftensTheEdge() {
        S s = build();
        show(*s.doc, s.b, false);                                         // only layer A matters here
        LayerFx fx; fx.masks << rectMask(QRectF(0, 0, 50, 100));
        s.doc->setLayerFx(s.a, fx, "Mask");
        QImage img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(20, 50)));
        QCOMPARE(img.pixelColor(80, 50).alpha(), 0);
        // soft edge: in-between alpha around x = 50, and wider than a hard edge
        fx.masks[0].feather = 12;
        s.doc->setLayerFx(s.a, fx, "Feather");
        img = render(*s.doc);
        int mid = 0; for (int x = 35; x < 65; ++x) { const int a = img.pixelColor(x, 10).alpha(); if (a > 20 && a < 235) ++mid; }
        QVERIFY2(mid >= 6, qPrintable(QString::number(mid)));
        // subtract mode removes instead, invert flips, intersect keeps the overlap only
        fx.masks[0].feather = 0; fx.masks[0].mode = MaskMode::Subtract;
        s.doc->setLayerFx(s.a, fx, "Subtract");
        img = render(*s.doc);
        QCOMPARE(img.pixelColor(20, 50).alpha(), 0); QVERIFY(isRed(img.pixelColor(80, 50)));
        fx.masks[0].mode = MaskMode::Add; fx.masks[0].invert = true;
        s.doc->setLayerFx(s.a, fx, "Invert");
        img = render(*s.doc);
        QCOMPARE(img.pixelColor(20, 50).alpha(), 0); QVERIFY(isRed(img.pixelColor(80, 50)));
        fx.masks[0].invert = false; fx.masks << rectMask(QRectF(25, 0, 50, 100), MaskMode::Intersect);
        s.doc->setLayerFx(s.a, fx, "Intersect");
        img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(40, 50))); QCOMPARE(img.pixelColor(10, 50).alpha(), 0); QCOMPARE(img.pixelColor(60, 50).alpha(), 0);
    }

    void aTrackMatteShowsALayerOnlyThroughAnotherAndHidesTheMatte() {
        S s = build();
        LayerFx fx; fx.matteLayer = s.b; fx.matteMode = MatteMode::Alpha;
        s.doc->setLayerFx(s.a, fx, "Matte");
        QImage img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(50, 50)));                          // inside the circle: A shows
        QCOMPARE(img.pixelColor(10, 10).alpha(), 0);                     // outside: nothing
        QVERIFY(!(img.pixelColor(50, 50).red() > 240 && img.pixelColor(50, 50).green() > 240));   // the white matte layer itself is not drawn
        fx.matteMode = MatteMode::InvertedAlpha;
        s.doc->setLayerFx(s.a, fx, "Inverted");
        img = render(*s.doc);
        QCOMPARE(img.pixelColor(50, 50).alpha(), 0); QVERIFY(isRed(img.pixelColor(10, 10)));
        // luma matte: white circle passes, a black one blocks
        fx.matteMode = MatteMode::Luma;
        s.doc->setLayerFx(s.a, fx, "Luma");
        img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(50, 50)));
        VectorShape dark = VectorShape::makeEllipse(QRectF(30, 30, 40, 40)); dark.fill.color = Qt::black; dark.stroke.enabled = false;
        s.doc->setShapes(s.b, {dark}, "dark");
        img = render(*s.doc);
        QCOMPARE(img.pixelColor(50, 50).alpha(), 0);
        // a matte that points back at its own layer must not hang
        LayerFx loop; loop.matteLayer = s.a; loop.matteMode = MatteMode::Alpha;
        s.doc->setLayerFx(s.b, loop, "Loop");
        render(*s.doc);
    }

    void effectsChangeThePixelsAsTheyShould() {
        S s = build();
        show(*s.doc, s.a, false);
        auto with = [&](Effect e) { LayerFx fx; fx.effects << e; s.doc->setLayerFx(s.b, fx, "Fx"); return render(*s.doc); };
        const QImage plain = render(*s.doc);
        QVERIFY(plain.pixelColor(50, 50).alpha() == 255 && plain.pixelColor(20, 50).alpha() == 0);
        // blur spreads the circle beyond its edge and keeps the middle solid-ish
        Effect bl = Effect::make(EffectType::Blur); bl.p[0] = 10;
        QImage img = with(bl);
        QVERIFY(img.pixelColor(26, 50).alpha() > 10);                    // x = 26 is 4 px outside the circle
        QVERIFY(img.pixelColor(50, 50).alpha() > 240);
        // invert turns white into black
        img = with(Effect::make(EffectType::Invert));
        QVERIFY(img.pixelColor(50, 50).red() < 10 && img.pixelColor(50, 50).alpha() == 255);
        // hue shift: red -> green (+120)
        show(*s.doc, s.a, true);
        Effect hue = Effect::make(EffectType::HueSaturation); hue.p[0] = 120;
        LayerFx fx; fx.effects << hue; s.doc->setLayerFx(s.a, fx, "Hue");
        img = render(*s.doc);
        QVERIFY(img.pixelColor(5, 5).green() > 240 && img.pixelColor(5, 5).red() < 15);
        s.doc->setLayerFx(s.a, LayerFx(), "None");
        // brightness lifts a mid grey
        show(*s.doc, s.a, false);
        VectorShape grey = VectorShape::makeRect(QRectF(0, 0, 100, 100)); grey.fill.color = QColor(100, 100, 100); grey.stroke.enabled = false;
        s.doc->setShapes(s.b, {grey}, "grey");
        Effect br = Effect::make(EffectType::BrightnessContrast); br.p[0] = 30;
        img = with(br);
        QVERIFY2(img.pixelColor(50, 50).red() > 160, qPrintable(QString::number(img.pixelColor(50, 50).red())));
        // drop shadow: a dark, partly transparent copy appears offset down-right of the shape
        VectorShape sq = VectorShape::makeRect(QRectF(20, 20, 30, 30)); sq.fill.color = Qt::white; sq.stroke.enabled = false;
        s.doc->setShapes(s.b, {sq}, "square");
        Effect sh = Effect::make(EffectType::DropShadow); sh.p[0] = 12; sh.p[1] = 12; sh.p[2] = 0; sh.p[3] = 100;
        img = with(sh);
        QVERIFY(img.pixelColor(35, 35).red() > 240);                      // the layer is still on top
        const QColor shadow = img.pixelColor(58, 58);                     // inside the shadow only
        QVERIFY2(shadow.alpha() > 200 && shadow.red() < 30, qPrintable(shadow.name(QColor::HexArgb)));
        // glow adds light around bright pixels
        Effect gl = Effect::make(EffectType::Glow); gl.p[0] = 10; gl.p[1] = 200;
        img = with(gl);
        QVERIFY(img.pixelColor(53, 35).alpha() > 20);
        // a disabled effect does nothing
        s.doc->setLayerFx(s.b, LayerFx(), "No fx");
        const QImage noFx = render(*s.doc);
        bl.enabled = false;
        const QImage disabled = with(bl);
        QVERIFY(disabled == noFx);
    }

    void effectsMasksAndMattesSaveLoadAndUndo() {
        S s = build();
        LayerFx fx; fx.masks << rectMask(QRectF(0, 0, 50, 100)); fx.masks[0].feather = 3; fx.masks[0].mode = MaskMode::Intersect; fx.masks[0].invert = true;
        Effect bl = Effect::make(EffectType::Blur); bl.p[0] = 4; fx.effects << bl;
        Effect sh = Effect::make(EffectType::DropShadow); sh.color = QColor(10, 20, 30); fx.effects << sh;
        fx.matteLayer = s.b; fx.matteMode = MatteMode::InvertedLuma;
        s.doc->setLayerFx(s.a, fx, "All");
        QTemporaryDir dir; QString err;
        QVERIFY2(ProjectIO::save(*s.doc, dir.filePath("t.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(dir.filePath("t.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY(r.doc->findLayer(s.a)->fx == fx);
        QCOMPARE(render(*r.doc), render(*s.doc));
        // undo / redo
        s.doc->undoStack()->undo();
        QVERIFY(!s.doc->findLayer(s.a)->fx.any());
        s.doc->undoStack()->redo();
        QVERIFY(s.doc->findLayer(s.a)->fx == fx);
        // dragging a slider is one undo step
        s.doc->undoStack()->clear();
        for (int v = 1; v <= 10; ++v) { LayerFx f = s.doc->findLayer(s.a)->fx; f.effects[0].p[0] = v; s.doc->setLayerFx(s.a, f, "Blur radius", "blur-" + s.a.toString()); }
        QCOMPARE(s.doc->undoStack()->count(), 1);
        // duplicating a layer keeps its effects
        const QUuid dup = s.doc->duplicateLayer(s.a);
        QVERIFY(s.doc->findLayer(dup)->fx.effects.size() == 2);
    }

    void effectsOnlyTouchThePartOfTheLayerThatIsRedrawn() {
        // partial redraws (the editor repaints dirty rectangles) must give the same pixels as a full render
        S s = build();
        Effect bl = Effect::make(EffectType::Blur); bl.p[0] = 15;
        LayerFx fx; fx.effects << bl; s.doc->setLayerFx(s.b, fx, "Blur");
        const QImage full = render(*s.doc);
        Compositor::Options o; o.includeBackground = false;
        const QRect part(20, 20, 30, 30);
        const QImage piece = Compositor::render(*s.doc, part, o);
        QCOMPARE(piece, full.copy(part));
    }
};

QTEST_MAIN(TestEffects)
#include "test_effects.moc"
