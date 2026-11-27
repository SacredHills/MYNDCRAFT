#include <QtTest>

#include "TestEnv.h"
#include "Core/Exporter.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"
#include "Scene3D/Anim3D.h"
#include "Scene3D/Render3D.h"

using namespace mynd;

// 2D layers shown as cards inside a 3D scene.
class TestLayer3D : public QObject {
    Q_OBJECT
    struct Setup { std::unique_ptr<Document> doc; QUuid art, scene, card, cam; };
    // 160x100 white canvas; a vector layer with a red 80x50 rectangle in the middle and a green 20x20 square at the left.
    Setup build(bool withCard = true) {
        CompositionSettings cs; cs.width = 160; cs.height = 100; cs.durationFrames = 12; cs.background = Qt::white;
        Setup s;
        s.doc = std::make_unique<Document>(cs);
        s.art = s.doc->addLayer(LayerType::Vector, "Art");
        VectorShape r = VectorShape::makeRect(QRectF(40, 25, 80, 50)); r.fill.color = Qt::red; r.stroke.enabled = false;
        VectorShape g = VectorShape::makeRect(QRectF(5, 5, 20, 20)); g.fill.color = Qt::green; g.stroke.enabled = false;
        s.doc->setShapes(s.art, {r, g}, "shapes");
        if (withCard) {
            s.scene = s.doc->addScene3DLayer("Scene");
            const double aspect = 1.6;
            s.doc->editScene3D(s.scene, [&](Scene3D& sc) {
                auto mesh = makeCardPlane(2 * aspect, 2); mesh->parts = {{0, int(mesh->indices.size()), QUuid()}}; sc.meshes[mesh->id] = mesh;
                Node3D n; n.kind = NodeKind::Mesh; n.name = "Card"; n.mesh = mesh->id; n.cardLayer = s.art; s.card = sc.addNode(n);
                Node3D c; c.kind = NodeKind::Camera; c.name = "Cam"; c.position = {0, 0, float(1.0 / std::tan(22.5 * M_PI / 180.0))}; s.cam = sc.addNode(c);
                sc.activeCamera = s.cam;
                return true; }, "Build");
        }
        s.doc->undoStack()->clear();
        return s;
    }
    static QImage render(const Document& d, int frame = 0) { Compositor::Options o; o.frame = frame; o.scene3dSupersample = 2; return Compositor::render(d, d.canvasRect(), o); }
    static double meanDiff(const QImage& a, const QImage& b) {
        double sum = 0;
        for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            sum += std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) + std::abs(p.blue() - q.blue());
        }
        return sum / (3.0 * a.width() * a.height());
    }
    static bool isRed(const QColor& c) { return c.red() > 200 && c.green() < 60 && c.blue() < 60; }
    static bool isWhite(const QColor& c) { return c.red() > 245 && c.green() > 245 && c.blue() > 245; }

    // ---- Phase 8: one composition holding traditional animation, vector art, text and 3D, animated together ----
    // 200x120 canvas, 12 frames. Bottom to top: hand-drawn animation (a red square that turns blue at frame 6), a 3D scene (a cube, a light, a camera,
    // transparent background) and, inside that scene, a vector layer as a card whose own 2D position slides 80 px to the right over the clip;
    // text on top.
    struct Comp { std::unique_ptr<Document> doc; QUuid anim, vec, scene, text, cube; };
    Comp buildComp() {
        CompositionSettings cs; cs.width = 200; cs.height = 120; cs.durationFrames = 12; cs.background = Qt::white;
        Comp c;
        c.doc = std::make_unique<Document>(cs);
        Document& d = *c.doc;
        c.anim = d.addAnimationLayer("Hand drawn");
        const QUuid r = d.createDrawing(c.anim, 0, false);
        { QPainter p(&d.findLayer(c.anim)->drawingImages[r]); p.fillRect(10, 80, 30, 30, Qt::red); }
        const QUuid b = d.createDrawing(c.anim, 6, false);
        { QPainter p(&d.findLayer(c.anim)->drawingImages[b]); p.fillRect(10, 80, 30, 30, Qt::blue); }
        c.vec = d.addLayer(LayerType::Vector, "Vector");
        VectorShape g = VectorShape::makeRect(QRectF(20, 10, 40, 30)); g.fill.color = QColor(0, 200, 0); g.stroke.enabled = false;
        d.setShapes(c.vec, {g}, "art");
        {   // 2D position animation: 0 -> 80 px over frames 0..10
            Layer* l = d.findLayer(c.vec);
            PropertyTracks t = l->tracks;
            const PropertyDef* pd = findPropertyDef("transform.position");
            PropertyTrack tr; tr.base = PropValue::of(0, 0);
            tr.setKey(*pd, 0, PropValue::of(0, 0));
            tr.setKey(*pd, 10, PropValue::of(80, 0));
            t.insert("transform.position", tr);
            d.editTracks(c.vec, t, "Slide");
        }
        c.scene = d.addScene3DLayer("Scene");
        d.editScene3D(c.scene, [&](Scene3D& sc) {
            auto cube = makeCube(1.0);
            sc.meshes[cube->id] = cube;
            Node3D n; n.kind = NodeKind::Mesh; n.name = "Cube"; n.mesh = cube->id; n.position = {0, 0, -3}; c.cube = sc.addNode(n);
            auto mesh = makeCardPlane(2 * 1.6667, 2); mesh->parts = {{0, int(mesh->indices.size()), QUuid()}}; sc.meshes[mesh->id] = mesh;
            Node3D card; card.kind = NodeKind::Mesh; card.name = "Card"; card.mesh = mesh->id; card.cardLayer = c.vec; sc.addNode(card);
            Node3D l; l.kind = NodeKind::Light; l.lightType = LightType::Directional; l.rotation = {-30, -30, 0}; sc.addNode(l);
            Node3D cam; cam.kind = NodeKind::Camera; cam.name = "Cam"; cam.position = {0, 0, float(1.0 / std::tan(22.5 * M_PI / 180.0))};
            sc.activeCamera = sc.addNode(cam);
            sc.background = QColor(0, 0, 0, 0);
            return true; }, "Build");
        TextContent tc; tc.text = "HELLO"; tc.pos = {120, 20}; tc.sizePx = 24; tc.color = Qt::black;
        c.text = d.addTextLayer(tc, "Title");
        d.undoStack()->clear();
        return c;
    }
    static bool nonWhite(const QColor& c) { return c.red() < 235 || c.green() < 235 || c.blue() < 235; }

private slots:
    void oneCompositionMixesAnimationVectorTextAndThreeDAndAnimatesThemTogether() {
        Comp c = buildComp();
        const QImage f0 = render(*c.doc, 0), f6 = render(*c.doc, 6), f10 = render(*c.doc, 10);
        // hand-drawn layer: red, then blue from frame 6
        QVERIFY(isRed(f0.pixelColor(25, 95)));
        QVERIFY(f6.pixelColor(25, 95).blue() > 200 && f6.pixelColor(25, 95).red() < 60);
        // the 3D cube is in the middle of every frame and is not a flat white
        for (const QImage* f : {&f0, &f6, &f10}) QVERIFY(nonWhite(f->pixelColor(100, 60)));
        // the card (the vector layer with its OWN 2D position animation) slides right: green at (40,25) on frame 0, gone from there and found 80 px further on frame 10
        auto green = [](const QColor& q) { return q.green() > 150 && q.red() < 80 && q.blue() < 80; };
        QVERIFY(green(f0.pixelColor(40, 25)));
        QVERIFY(!green(f10.pixelColor(40, 25)));
        QVERIFY2(green(f10.pixelColor(120, 25)), qPrintable(f10.pixelColor(120, 25).name()));
        // text sits above everything
        bool text = false;
        for (int y = 20; y < 45 && !text; ++y) for (int x = 120; x < 180; ++x) if (f0.pixelColor(x, y).red() < 90) { text = true; break; }
        QVERIFY(text);
        // save, reopen, render the entire composition again: identical frames
        QTemporaryDir dir; QString err;
        QVERIFY2(ProjectIO::save(*c.doc, dir.filePath("comp.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(dir.filePath("comp.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        for (int f : {0, 3, 6, 10}) QVERIFY2(meanDiff(render(*r.doc, f), render(*c.doc, f)) < 0.01, qPrintable(QString::number(f)));
        // and the final render: every frame of the reopened project exports exactly what the editor shows, 2D + 3D together
        exporter::ExportOptions eo; eo.firstFrame = 0; eo.lastFrame = 11;
        QStringList written;
        QVERIFY2(exporter::writePngSequence(*r.doc, dir.path(), "out", eo, &written, &err), qPrintable(err));
        QCOMPARE(written.size(), 12);
        for (int f : {0, 6, 11}) {
            QImage img(written[f]);
            QVERIFY2(meanDiff(img.convertToFormat(QImage::Format_ARGB32), render(*c.doc, f).convertToFormat(QImage::Format_ARGB32)) < 0.5, qPrintable(QString::number(f)));
        }
        // with a transparent background, only the content is left (cube, card, drawing, text), the empty corner has no alpha
        eo.transparentBackground = true; eo.firstFrame = eo.lastFrame = 0; written.clear();
        QVERIFY2(exporter::writePngSequence(*r.doc, dir.path(), "alpha", eo, &written, &err), qPrintable(err));
        const QImage a(written.first());
        QCOMPARE(a.pixelColor(195, 115).alpha(), 0);
        QVERIFY(a.pixelColor(100, 60).alpha() > 250);                           // the cube
        QVERIFY(a.pixelColor(25, 95).alpha() > 250);                            // the hand-drawn square
    }
    void aCardInFrontOfTheDefaultCameraLooksLikeTheFlatLayer() {
        Setup flat = build(false), card = build(true);
        const QImage a = render(*flat.doc), b = render(*card.doc);
        QVERIFY2(meanDiff(a, b) < 3.0, qPrintable(QString::number(meanDiff(a, b))));
        QVERIFY(isRed(b.pixelColor(80, 50)));
        QCOMPARE(b.pixelColor(15, 15).green() > 200 && b.pixelColor(15, 15).red() < 60, true);
        QVERIFY(isWhite(b.pixelColor(150, 90)));                              // the white background shows through where the card has no content
    }

    void theSourceLayerIsOnlyDrawnThroughItsCard() {
        Setup s = build(true);
        // push the card out of the picture: nothing of the layer may remain in the composition
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->position = {50, 0, 0}; return true; }, "Away");
        const QImage img = render(*s.doc);
        QVERIFY(isWhite(img.pixelColor(80, 50)));
        QVERIFY(isWhite(img.pixelColor(15, 15)));
        // deleting the card brings the layer back
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.removeNode(s.card); return true; }, "Remove card");
        QVERIFY(isRed(render(*s.doc).pixelColor(80, 50)));
        // a card whose layer was deleted shows nothing and does not crash
        Setup t = build(true);
        t.doc->removeLayer(t.art);
        QVERIFY(isWhite(render(*t.doc).pixelColor(80, 50)));
    }

    void cardsHaveRealThreeDTransforms() {
        Setup s = build(true);
        const QImage flat = render(*s.doc);
        // turn it 70° about Y: it gets narrower, so the left and right parts of the rectangle fall away
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->rotation = {0, 70, 0}; return true; }, "Turn");
        const QImage turned = render(*s.doc);
        QVERIFY(isRed(turned.pixelColor(80, 50)));
        QVERIFY(!isRed(turned.pixelColor(112, 50)) || isRed(flat.pixelColor(112, 50)));
        int redFlat = 0, redTurned = 0;
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 160; ++x) { redFlat += isRed(flat.pixelColor(x, y)); redTurned += isRed(turned.pixelColor(x, y)); }
        QVERIFY2(redTurned < redFlat * 0.7 && redTurned > 0, qPrintable(QString("%1 vs %2").arg(redTurned).arg(redFlat)));
        // moving it back (+Z toward the camera) enlarges it; moving away shrinks it
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->rotation = {0, 0, 0}; sc.node(s.card)->position = {0, 0, 1}; return true; }, "Closer");
        int redNear = 0; const QImage near = render(*s.doc);
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 160; ++x) redNear += isRed(near.pixelColor(x, y));
        QVERIFY(redNear > redFlat * 1.3);
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->position = {0, 0, -4}; return true; }, "Further");
        int redFar = 0; const QImage far = render(*s.doc);
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 160; ++x) redFar += isRed(far.pixelColor(x, y));
        QVERIFY(redFar < redFlat * 0.5 && redFar > 0);
    }

    void cardsAreDepthSortedAgainstObjectsAndEachOther() {
        Setup s = build(true);
        QUuid cube;
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) {
            Material m; m.id = QUuid::createUuid(); m.baseColor = Qt::blue; m.roughness = 1; m.unlit = true; sc.materials.push_back(m);
            auto c = makeCube(0.4); c->parts = {{0, int(c->indices.size()), m.id}}; sc.meshes[c->id] = c;
            Node3D n; n.kind = NodeKind::Mesh; n.name = "Cube"; n.mesh = c->id; n.position = {0, 0, 0.6f}; cube = sc.addNode(n);   // in front of the card
            return true; }, "Cube in front");
        QImage img = render(*s.doc);
        QVERIFY(img.pixelColor(80, 50).blue() > 200 && img.pixelColor(80, 50).red() < 60);       // the cube hides the middle of the card
        QVERIFY(isRed(img.pixelColor(60, 35)) || isRed(img.pixelColor(45, 30)));                   // ... but not the rest
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(cube)->position = {0, 0, -0.6f}; return true; }, "Cube behind");
        img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(80, 50)));                                                    // now the card hides the cube
        // two cards crossing: the nearer one is on top regardless of node order
        const QUuid art2 = s.doc->addLayer(LayerType::Vector, "Art2");
        VectorShape b = VectorShape::makeRect(QRectF(0, 0, 160, 100)); b.fill.color = Qt::blue; b.stroke.enabled = false;
        s.doc->setShapes(art2, {b}, "blue");
        QUuid card2;
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) {
            sc.node(cube)->visible = false;
            auto mesh = makeCardPlane(3.2, 2); mesh->parts = {{0, int(mesh->indices.size()), QUuid()}}; sc.meshes[mesh->id] = mesh;
            Node3D n; n.kind = NodeKind::Mesh; n.name = "Card2"; n.mesh = mesh->id; n.cardLayer = art2; n.position = {0, 0, -1.0f}; n.scale = {1.8f, 1.8f, 1};     // behind card 1, listed after it
            card2 = sc.addNode(n);
            return true; }, "Second card");
        img = render(*s.doc);
        QVERIFY(isRed(img.pixelColor(80, 50)));                                                    // red card is nearer: on top of the blue one
        QVERIFY(img.pixelColor(10, 60).blue() > 200 && img.pixelColor(10, 60).red() < 80);         // blue shows where the red layer has no content
    }

    void layerOpacityLitCardsAnd2DAnimationReachTheCard() {
        Setup s = build(true);
        LayerProps p = s.doc->findLayer(s.art)->props; p.opacity = 0.5; s.doc->setLayerProps(s.art, p);
        QColor c = render(*s.doc).pixelColor(80, 50);
        QVERIFY2(c.red() > 230 && c.green() > 100 && c.green() < 160, qPrintable(c.name()));     // half-transparent red over white
        p.opacity = 1.0; s.doc->setLayerProps(s.art, p);
        // a lit card is shaded by lights; without lights it only gets the ambient level
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->cardLit = true; sc.ambientIntensity = 0.5; return true; }, "Lit");
        c = render(*s.doc).pixelColor(80, 50);
        QVERIFY2(c.red() < 160 && c.red() > 60, qPrintable(c.name()));
        // the card moves with 3D keyframes like any node
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->cardLit = false; return true; }, "Flat");
        s.doc->setCurrentFrame(0);
        s.doc->toggleScene3DAnimation(s.scene, nodePropId(s.card, "pos"));
        s.doc->setCurrentFrame(6);
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->position = {2.2f, 0, 0}; return true; }, "Slide");
        QVERIFY(isRed(render(*s.doc, 0).pixelColor(80, 50)));
        QVERIFY(isWhite(render(*s.doc, 6).pixelColor(80, 50)));
        // and the layer's own contents animate: change the art, the card follows
        s.doc->setCurrentFrame(0);
        VectorShape g = VectorShape::makeRect(QRectF(0, 0, 160, 100)); g.fill.color = Qt::yellow; g.stroke.enabled = false;
        s.doc->setShapes(s.art, {g}, "repaint");
        const QColor y = render(*s.doc, 0).pixelColor(80, 50);
        QVERIFY2(y.red() > 230 && y.green() > 230 && y.blue() < 40, qPrintable(y.name()));
    }

    void cardsSurviveSavingAndDuplicatingTheSceneLayer() {
        Setup s = build(true);
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->rotation = {0, 25, 0}; sc.node(s.card)->cardLit = false; return true; }, "Turn");
        QTemporaryDir dir; QString err;
        QVERIFY2(ProjectIO::save(*s.doc, dir.filePath("c.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(dir.filePath("c.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        QCOMPARE(r.doc->findLayer(s.scene)->scene3d->node(s.card)->cardLayer, s.art);
        QVERIFY(meanDiff(render(*r.doc), render(*s.doc)) < 0.01);
        // a card that points at a scene layer (even its own) draws nothing, and the layer it used to show is drawn normally again
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->cardLayer = s.scene; return true; }, "Self");
        QVERIFY(isRed(render(*s.doc).pixelColor(80, 50)));
    }
};

QTEST_MAIN(TestLayer3D)
#include "test_layer3d.moc"
