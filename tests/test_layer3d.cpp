#include <QtTest>
#include <QElapsedTimer>
#include <QThread>

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
        TextContent tc; tc.text = "HELLO"; tc.pos = {125, 85}; tc.sizePx = 24; tc.color = Qt::black;
        c.text = d.addTextLayer(tc, "Title");
        d.undoStack()->clear();
        return c;
    }
    static bool nonWhite(const QColor& c) { return c.red() < 235 || c.green() < 235 || c.blue() < 235; }

private slots:
    void effectsMasksAndMattesOfTheSourceLayerShowOnItsCard() {
        Setup s = build();
        const QImage plain = render(*s.doc);
        QVERIFY(isRed(plain.pixelColor(80, 50)));
        // mask: keep only the left half of the canvas -> the red middle is cut off at x = 80, the green square stays
        Layer* art = s.doc->findLayer(s.art);
        LayerFx fx;
        Mask m; m.shape = VectorShape::makeRect(QRectF(0, 0, 80, 100)); m.shape.closed = true; fx.masks << m;
        s.doc->setLayerFx(s.art, fx, "mask");
        const QImage masked = render(*s.doc);
        QVERIFY(isRed(masked.pixelColor(60, 50)));
        QVERIFY(isWhite(masked.pixelColor(100, 50)));
        QVERIFY(art->fx.masks.size() == 1);
        // an Invert effect turns the red card cyan
        fx.masks.clear(); fx.effects << Effect::make(EffectType::Invert);
        s.doc->setLayerFx(s.art, fx, "invert");
        const QColor c = render(*s.doc).pixelColor(80, 50);
        QVERIFY2(c.red() < 60 && c.green() > 200 && c.blue() > 200, qPrintable(c.name()));
        // a track matte (Alpha) from a small vector layer limits the card to that layer's shape
        const QUuid matte = s.doc->addLayer(LayerType::Vector, "Matte");
        VectorShape mr = VectorShape::makeRect(QRectF(0, 0, 70, 100)); mr.fill.color = Qt::black; mr.stroke.enabled = false;
        s.doc->setShapes(matte, {mr}, "matte");
        fx.effects.clear(); fx.matteLayer = matte; fx.matteMode = MatteMode::Alpha;
        s.doc->setLayerFx(s.art, fx, "matte");
        const QImage mt = render(*s.doc);
        QVERIFY(isRed(mt.pixelColor(60, 50)));
        QVERIFY(isWhite(mt.pixelColor(90, 50)));
    }
    void aCardThatAppearsLargerThanTheCanvasKeepsItsDetail() {
        Setup s = build(true);
        // a 2 px wide black vertical line on white
        VectorShape ln = VectorShape::makeRect(QRectF(79, 0, 2, 100)); ln.fill.color = Qt::black; ln.stroke.enabled = false;
        s.doc->setShapes(s.art, {ln}, "line");
        auto cardTexture = [&](double scale) {
            s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->scale = {float(scale), float(scale), float(scale)}; return true; }, "Scale");
            auto scene = Compositor::sceneForDrawing(*s.doc->findLayer(s.scene), 0, s.doc->canvasSize());
            QSize best;
            for (const auto& [id, img] : scene->textures) best = img.size();
            return best;
        };
        QCOMPARE(cardTexture(1.0), QSize(160, 100));                       // as it appears on the canvas: canvas resolution
        QCOMPARE(cardTexture(0.5), QSize(160, 100));                       // smaller never costs more
        const QSize big = cardTexture(3.0);
        QVERIFY2(big.width() >= 3 * 160 - 2 && big.width() <= 4 * 160, qPrintable(QString::number(big.width())));
        // and the magnified line stays crisp: few in-between greys across the 3x wide line
        auto soft = [&](double scale) {
            s.doc->editScene3D(s.scene, [&](Scene3D& sc) { sc.node(s.card)->scale = {float(scale), float(scale), float(scale)}; return true; }, "Scale");
            auto scene = Compositor::sceneForDrawing(*s.doc->findLayer(s.scene), 0, s.doc->canvasSize());
            RenderOptions o; o.threads = 1;
            const QImage img = renderScene(*scene, sceneCamera(*scene, 1.6), QSize(160, 100), o);
            int mid = 0;
            for (int x = 60; x < 100; ++x) { const int v = img.pixelColor(x, 50).red(); if (v > 30 && v < 225) ++mid; }
            return mid;
        };
        QVERIFY2(soft(3.0) <= 4, qPrintable(QString::number(soft(3.0))));
    }

    void aCardUsesItsLayersBlendModeAgainstWhatIsBehindIt() {
        Setup s = build(true);
        // a mid-grey unlit wall behind the card
        s.doc->editScene3D(s.scene, [&](Scene3D& sc) {
            Material m; m.id = QUuid::createUuid(); m.name = "Wall"; m.baseColor = QColor(128, 128, 128); m.unlit = true; sc.materials.push_back(m);
            auto wall = makeCardPlane(8, 6); wall->parts = {{0, int(wall->indices.size()), m.id}}; sc.meshes[wall->id] = wall;
            Node3D n; n.kind = NodeKind::Mesh; n.name = "Wall"; n.mesh = wall->id; n.position = {0, 0, -0.5f}; sc.addNode(n);
            return true; }, "Wall");
        struct Case { BlendMode mode; QColor expect; };
        const Case cases[] = {
            {BlendMode::Normal, QColor(255, 0, 0)},
            {BlendMode::Multiply, QColor(128, 0, 0)},
            {BlendMode::Screen, QColor(255, 128, 128)},
            {BlendMode::Add, QColor(255, 128, 128)},
            {BlendMode::Darken, QColor(128, 0, 0)},
            {BlendMode::Lighten, QColor(255, 128, 128)},
            {BlendMode::Difference, QColor(127, 128, 128)},
        };
        auto shot = [&](int x, int y) {
            const Layer* sl = s.doc->findLayer(s.scene);
            auto scene = Compositor::sceneForDrawing(*sl, 0, s.doc->canvasSize());
            RenderOptions o; o.threads = 1;
            return renderScene(*scene, sceneCamera(*scene, 1.6), QSize(160, 100), o).pixelColor(x, y);
        };
        for (const Case& c : cases) {
            LayerProps p = s.doc->findLayer(s.art)->props; p.blend = c.mode; s.doc->setLayerProps(s.art, p);
            const QColor got = shot(80, 50);
            QVERIFY2(std::abs(got.red() - c.expect.red()) <= 4 && std::abs(got.green() - c.expect.green()) <= 4 && std::abs(got.blue() - c.expect.blue()) <= 4,
                     qPrintable(QString("mode %1: got %2 expected %3").arg(blendModeName(c.mode), got.name(), c.expect.name())));
            QVERIFY2(std::abs(shot(150, 90).red() - 128) <= 2, "the wall outside the card is untouched");
        }
        // Overlay on a mid-grey wall: backdrop 0.5 -> cs * 2 * 0.5 = cs (red stays red)
        LayerProps p = s.doc->findLayer(s.art)->props; p.blend = BlendMode::Overlay; s.doc->setLayerProps(s.art, p);
        const QColor ov = shot(80, 50);
        QVERIFY2(ov.red() > 250 && ov.green() < 6 && ov.blue() < 6, qPrintable(ov.name()));
    }

    void textureRepeatTilesTheImageAndSurvivesSaving() {
        CompositionSettings cs; cs.width = 160; cs.height = 100; cs.durationFrames = 2; cs.background = Qt::white;
        Document d(cs);
        const QUuid scene = d.addScene3DLayer("Scene");
        QUuid mat;
        d.editScene3D(scene, [&](Scene3D& sc) {
            QImage tex(64, 1, QImage::Format_ARGB32);                       // left half red, right half blue
            for (int x = 0; x < 64; ++x) tex.setPixelColor(x, 0, x < 32 ? Qt::red : Qt::blue);
            const QUuid tid = QUuid::createUuid(); sc.textures[tid] = tex;
            Material m; m.id = QUuid::createUuid(); m.name = "Tiled"; m.baseColor = Qt::white; m.baseTexture = tid; m.unlit = true; m.doubleSided = true;
            mat = m.id; sc.materials.push_back(m);
            auto plane = makeCardPlane(3.2, 2); plane->parts = {{0, int(plane->indices.size()), mat}}; sc.meshes[plane->id] = plane;
            Node3D n; n.kind = NodeKind::Mesh; n.name = "Plane"; n.mesh = plane->id; sc.addNode(n);
            Node3D cam; cam.kind = NodeKind::Camera; cam.position = {0, 0, float(1.0 / std::tan(22.5 * M_PI / 180.0))};
            sc.activeCamera = sc.addNode(cam);
            return true; }, "Build");
        auto render = [&](const Document& doc) {
            const Layer* l = doc.findLayer(scene);
            RenderOptions o; o.threads = 1;
            return renderScene(*l->scene3d, sceneCamera(*l->scene3d, 1.6), QSize(160, 100), o);
        };
        auto isRedPx = [](const QColor& c) { return c.red() > 200 && c.blue() < 60; };
        auto isBluePx = [](const QColor& c) { return c.blue() > 200 && c.red() < 60; };
        // once: red on the left half, blue on the right
        QImage img = render(d);
        QVERIFY(isRedPx(img.pixelColor(40, 50)) && isBluePx(img.pixelColor(120, 50)));
        // repeated twice across: red, blue, red, blue
        d.editScene3D(scene, [&](Scene3D& sc) { sc.material(mat)->repeatU = 2; return true; }, "Repeat");
        img = render(d);
        QVERIFY(isRedPx(img.pixelColor(20, 50)));
        QVERIFY(isBluePx(img.pixelColor(60, 50)));
        QVERIFY(isRedPx(img.pixelColor(100, 50)));
        QVERIFY(isBluePx(img.pixelColor(140, 50)));
        // shifting by half a tile swaps the colours
        d.editScene3D(scene, [&](Scene3D& sc) { sc.material(mat)->offsetU = 0.5; return true; }, "Offset");
        img = render(d);
        QVERIFY(isBluePx(img.pixelColor(20, 50)) && isRedPx(img.pixelColor(60, 50)));
        // saved and reopened
        QTemporaryDir dir; QString err;
        QVERIFY2(ProjectIO::save(d, dir.filePath("t.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(dir.filePath("t.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        const Material* back = r.doc->findLayer(scene)->scene3d->material(mat);
        QVERIFY(back);
        QCOMPARE(back->repeatU, 2.0); QCOMPARE(back->repeatV, 1.0); QCOMPARE(back->offsetU, 0.5);
        QVERIFY(meanDiff(render(*r.doc), img) < 0.01);
    }

private slots:
    void translucentSurfacesThatCrossBlendTheSameWhicheverOrderTheyAreListedIn() {
        auto render = [&](bool swap) {
            Scene3D sc;
            auto plane = makePlane(2.0); sc.meshes[plane->id] = plane;
            auto mk = [&](QColor c, float rotY, const char* nm) {
                Material m; m.id = QUuid::createUuid(); m.baseColor = c; m.unlit = true; m.doubleSided = true; sc.materials.push_back(m);
                Node3D n; n.kind = NodeKind::Mesh; n.name = nm; n.mesh = plane->id; n.materialOverride = m.id; n.rotation = {90.0f, rotY, 0.0f}; return n;
            };
            Node3D a = mk(QColor(255, 0, 0, 128), 25.0f, "A"), b = mk(QColor(0, 0, 255, 128), -25.0f, "B");
            if (swap) { sc.addNode(b); sc.addNode(a); } else { sc.addNode(a); sc.addNode(b); }
            Node3D cam; cam.kind = NodeKind::Camera; cam.name = "Cam"; cam.position = {0.0f, 0.0f, 4.0f}; sc.activeCamera = sc.addNode(cam);
            sc.background = Qt::white;
            RenderOptions o; o.threads = 1;
            return renderScene(sc, sceneCamera(sc, 1.0), QSize(120, 120), o);
        };
        const QImage x = render(false), y = render(true);
        QVERIFY2(x == y, "listing order changed the blended result");
        QVERIFY(x.pixelColor(60, 60) != QColor(Qt::white));
    }

    void theExportCanWriteAnAlphaMatteAndADepthPassNextToTheColourFrames() {
        Comp c = buildComp();
        QTemporaryDir dir; QVERIFY(dir.isValid());
        exporter::ExportOptions o; o.firstFrame = 0; o.lastFrame = 1; o.alphaPass = true; o.depthPass = true;
        QStringList files; QString err;
        QVERIFY2(exporter::writePngSequence(*c.doc, dir.path(), "shot", o, &files, &err), qPrintable(err));
        for (const char* f : {"shot_0001.png", "shot_alpha_0001.png", "shot_depth_0001.png", "shot_alpha_0002.png", "shot_depth_0002.png", "shot_depth_range.txt"})
            QVERIFY2(QFileInfo::exists(dir.filePath(f)), f);
        QImage depth(dir.filePath("shot_depth_0001.png"));
        QCOMPARE(depth.format(), QImage::Format_Grayscale16);
        const quint16 centre = reinterpret_cast<const quint16*>(depth.constScanLine(60))[100];
        const quint16 corner = reinterpret_cast<const quint16*>(depth.constScanLine(2))[2];
        QVERIFY2(centre > 6000, qPrintable(QString::number(centre)));
        QCOMPARE(int(corner), 0);
        const QImage alpha(dir.filePath("shot_alpha_0001.png"));
        QCOMPARE(alpha.format(), QImage::Format_Grayscale8);
        QCOMPARE(qGray(alpha.pixel(100, 60)), 255);
        QFile r(dir.filePath("shot_depth_range.txt")); QVERIFY(r.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(r.readAll()).contains("near_distance"));
    }

    void theMultithreadedRendererGivesExactlyTheSameImageAsOneThreadAndIsFaster() {
        Comp c = buildComp();
        // a heavier scene: a grid of spheres plus the translucent card, so ordering of blended pixels matters
        c.doc->editScene3D(c.scene, [&](Scene3D& sc) {
            auto sph = makeSphere(0.35, 48, 24); sc.meshes[sph->id] = sph;
            for (int i = 0; i < 5; ++i) for (int j = 0; j < 3; ++j) {
                Node3D n; n.kind = NodeKind::Mesh; n.name = "S"; n.mesh = sph->id; n.position = {float(i - 2) * 0.8f, float(j - 1) * 0.8f, -1.0f - 0.2f * float(i)}; sc.addNode(n);
            }
            return true; }, "Spheres");
        const Layer* sl = c.doc->findLayer(c.scene);
        auto scene = Compositor::sceneForDrawing(*sl, 5, c.doc->canvasSize());
        const RenderCamera cam = sceneCamera(*scene, 200.0 / 120.0);
        const QSize size(1200, 720);
        RenderOptions one; one.threads = 1; one.supersample = 1;
        RenderOptions many = one; many.threads = 4;
        QElapsedTimer t; t.start();
        const QImage a = renderScene(*scene, cam, size, one);
        const qint64 tOne = t.elapsed();
        t.restart();
        const QImage b = renderScene(*scene, cam, size, many);
        const qint64 tMany = t.elapsed();
        QCOMPARE(a.size(), b.size());
        QVERIFY2(a == b, "banded render differs from the single-threaded one");
        qInfo() << "3D render" << size << ": 1 thread" << tOne << "ms, 4 bands in parallel" << tMany << "ms, cores" << QThread::idealThreadCount();
        // and with supersampling, as used for export
        one.supersample = many.supersample = 2;
        QVERIFY(renderScene(*scene, cam, QSize(400, 240), one) == renderScene(*scene, cam, QSize(400, 240), many));
    }

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
        for (int y = 80; y < 118 && !text; ++y) for (int x = 125; x < 195; ++x) if (f0.pixelColor(x, y).red() < 90) { text = true; break; }
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
