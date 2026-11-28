#include <QtTest>
#include <QtConcurrent>
#include <QPainter>

#include "TestEnv.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"
#include "Scene3D/Render3D.h"
#include "Scene3D/RenderGpu.h"
#include "Scene3D/Scene3D.h"

using namespace mynd;

// The OpenGL 3D renderer against the CPU renderer. Needs an OpenGL 3.3 context; with the "offscreen" platform none exists and the tests skip
// (run under Xvfb with QT_QPA_PLATFORM=xcb and Mesa's software driver, as the Linux CI job does).
class TestGpuRender3D : public QObject {
    Q_OBJECT
    bool m_ok = false;

    static Material makeMat(QColor c, double metal = 0, double rough = 0.6) { Material m; m.id = QUuid::createUuid(); m.baseColor = c; m.metallic = metal; m.roughness = rough; return m; }
    static QUuid addMesh(Scene3D& s, std::shared_ptr<Mesh> m, const Material& mat, QVector3D pos = {}, QVector3D rot = {}, QVector3D scale = {1, 1, 1}) {
        s.materials.push_back(mat);
        m->parts = {{0, int(m->indices.size()), mat.id}};
        s.meshes[m->id] = m;
        Node3D n; n.kind = NodeKind::Mesh; n.name = m->name; n.mesh = m->id; n.position = pos; n.rotation = rot; n.scale = scale;
        return s.addNode(n);
    }
    static void addLight(Scene3D& s, LightType t, QVector3D pos, QVector3D rot, QColor c = Qt::white, double intensity = 1.0, double range = 0, double spot = 45) {
        Node3D l; l.kind = NodeKind::Light; l.lightType = t; l.position = pos; l.rotation = rot; l.lightColor = c; l.intensity = intensity; l.range = range; l.spotAngle = spot; l.name = "Light";
        s.addNode(l);
    }
    static void addCamera(Scene3D& s) {
        Node3D c; c.kind = NodeKind::Camera; c.name = "Cam"; c.position = {0, 1.2f, 5.5f}; c.rotation = {-10, 0, 0};
        s.activeCamera = s.addNode(c);
    }
    static Scene3D basicScene() {
        Scene3D s;
        s.background = QColor(30, 34, 44);
        addMesh(s, makeCube(1.2), makeMat(QColor(200, 70, 60), 0.0, 0.5), {-1.3f, 0.2f, 0}, {20, 35, 0});
        addMesh(s, makeSphere(0.8, 40, 20), makeMat(QColor(220, 190, 80), 0.9, 0.3), {1.1f, 0.3f, -0.5f});
        Material floor = makeMat(QColor(90, 120, 160), 0, 0.9); floor.doubleSided = true;
        addMesh(s, makePlane(8), floor, {0, -0.7f, 0});
        addLight(s, LightType::Directional, {0, 5, 0}, {-50, 30, 0}, QColor(255, 244, 230), 1.1);
        addLight(s, LightType::Point, {-2.5f, 2.5f, 2.5f}, {}, QColor(120, 160, 255), 2.0, 12);
        addCamera(s);
        return s;
    }
    static QImage texture() {
        QImage t(64, 64, QImage::Format_ARGB32);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) t.setPixelColor(x, y, ((x / 8 + y / 8) % 2) ? QColor(240, 240, 60) : QColor(40, 80, 200, 255));
        return t;
    }
    // fraction of pixels whose largest channel difference exceeds `thr`, and the mean absolute difference
    static void metrics(const QImage& a, const QImage& b, int thr, double* frac, double* mean) {
        qint64 bad = 0; double sum = 0;
        for (int y = 0; y < a.height(); ++y) {
            const QRgb* p = reinterpret_cast<const QRgb*>(a.constScanLine(y)); const QRgb* q = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                const int d = std::max({std::abs(qRed(p[x]) - qRed(q[x])), std::abs(qGreen(p[x]) - qGreen(q[x])), std::abs(qBlue(p[x]) - qBlue(q[x])), std::abs(qAlpha(p[x]) - qAlpha(q[x]))});
                if (d > thr) ++bad;
                sum += d;
            }
        }
        *frac = double(bad) / (double(a.width()) * a.height()); *mean = sum / (double(a.width()) * a.height());
    }
    void same(const Scene3D& s, int ss = 1, double maxFrac = 0.015, double maxMean = 1.2) {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        const QSize size(240, 160);
        RenderOptions o; o.supersample = ss;
        const RenderCamera cam = sceneCamera(s, double(size.width()) / size.height());
        const QImage cpu = renderScene(s, cam, size, o);
        const int before = gpu3d::rendersDone();
        const QImage gpu = gpu3d::render(s, cam, size, o);
        QVERIFY2(!gpu.isNull(), "the GPU renderer declined this scene");
        QCOMPARE(gpu3d::rendersDone(), before + 1);
        QCOMPARE(gpu.size(), cpu.size());
        QCOMPARE(gpu.format(), cpu.format());
        double frac, mean; metrics(cpu, gpu, 24, &frac, &mean);
        qInfo("  differs by >24 levels in %.3f%% of pixels, mean %.3f", frac * 100, mean);
        if (qEnvironmentVariableIsSet("GPU3D_SAVE") || frac > maxFrac || mean > maxMean) { cpu.save("/tmp/gpu3d_cpu.png"); gpu.save("/tmp/gpu3d_gpu.png"); }
        QVERIFY2(frac <= maxFrac && mean <= maxMean, qPrintable(QString("%1% of pixels differ by more than 24 levels, mean difference %2").arg(frac * 100).arg(mean)));
        // and the picture is not empty: something other than the background was drawn
        int nonBg = 0;
        for (int y = 0; y < gpu.height(); y += 3) for (int x = 0; x < gpu.width(); x += 3) if (gpu.pixel(x, y) != cpu.pixel(2, 2)) ++nonBg;
        QVERIFY(nonBg > 50);
    }

private slots:
    void initTestCase() { m_ok = gpu3d::initialize(); if (m_ok) qInfo("GPU 3D: %s", qPrintable(gpu3d::rendererName())); }
    void cleanupTestCase() { gpu3d::shutdown(); }

    void litPrimitivesWithMetalAndRoughnessMatchTheCpu() { same(basicScene()); }
    void supersampledRenderMatchesTheCpu() { same(basicScene(), 2); }
    void emissiveUnlitAndSpotLightsMatchTheCpu() {
        Scene3D s = basicScene();
        Material glow = makeMat(QColor(60, 60, 60)); glow.emissive = QColor(0, 120, 40);
        addMesh(s, makeSphere(0.35, 24, 12), glow, {0, 1.2f, 1.0f});
        Material flat = makeMat(QColor(255, 120, 200)); flat.unlit = true;
        addMesh(s, makeCube(0.5), flat, {-0.2f, -0.35f, 1.6f});
        addLight(s, LightType::Spot, {0, 4, 3}, {-60, 0, 0}, QColor(255, 255, 200), 3.0, 14, 40);
        same(s);
    }
    void texturesWithRepeatAndOffsetMatchTheCpu() {
        Scene3D s = basicScene();
        const QUuid tid = QUuid::createUuid();
        s.textures[tid] = texture();
        Material m = makeMat(Qt::white, 0, 0.7); m.baseTexture = tid; m.repeatU = 2.5; m.repeatV = 1.5; m.offsetU = 0.3; m.offsetV = 0.1;
        addMesh(s, makeCube(1.0), m, {0, -0.1f, 1.8f}, {15, -25, 0});
        same(s);
    }
    void aMeshWithoutNormalsIsShadedFlatLikeTheCpu() {
        Scene3D s = basicScene();
        auto cube = makeCube(0.9); cube->normals.clear();
        addMesh(s, cube, makeMat(QColor(90, 200, 120), 0.2, 0.4), {0.2f, 1.1f, 0.6f}, {30, 20, 10});
        same(s);
    }
    void mirroredObjectsKeepTheirFacesAndLighting() {
        Scene3D s = basicScene();
        addMesh(s, makeCube(0.8), makeMat(QColor(200, 100, 220)), {0.0f, 1.2f, 0.2f}, {10, 30, 0}, {-1, 1, 1});
        auto flat = makeSphere(0.4, 16, 8); flat->normals.clear();
        addMesh(s, flat, makeMat(QColor(100, 220, 220)), {-1.8f, 1.3f, 0.8f}, {}, {1, -1, 1});
        same(s);
    }
    void seeThroughSurfacesAreBlendedFarToNearIncludingIntersectingOnes() {
        Scene3D s = basicScene();
        addMesh(s, makeSphere(0.9, 32, 16), makeMat(QColor(80, 160, 255, 110), 0, 0.3), {0.2f, 0.3f, 1.3f});
        Material pane = makeMat(QColor(255, 80, 80, 120)); pane.doubleSided = true;
        addMesh(s, makeCardPlane(2.2, 1.6), pane, {-0.6f, 0.3f, 0.9f}, {0, 30, 0});
        Material pane2 = makeMat(QColor(80, 255, 120, 120)); pane2.doubleSided = true;
        addMesh(s, makeCardPlane(2.2, 1.6), pane2, {-0.6f, 0.3f, 0.9f}, {0, -30, 0});      // crosses the first one
        same(s, 1, 0.02, 1.5);
    }
    void aCardWithATransparentTextureMatchesTheCpu() {
        Scene3D s = basicScene();
        QImage t(128, 80, QImage::Format_ARGB32_Premultiplied); t.fill(Qt::transparent);
        QPainter p(&t); p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor(255, 140, 0)); p.setPen(Qt::NoPen); p.drawEllipse(QRectF(10, 8, 100, 64)); p.end();
        const QUuid tid = QUuid::createUuid(); s.textures[tid] = t;
        Material m = makeMat(Qt::white); m.baseTexture = tid; m.unlit = true; m.doubleSided = true;
        auto card = makeCardPlane(2.6, 1.6);
        s.materials.push_back(m); card->parts = {{0, int(card->indices.size()), m.id}}; s.meshes[card->id] = card;
        Node3D n; n.kind = NodeKind::Mesh; n.name = "Card"; n.mesh = card->id; n.position = {0.3f, 0.5f, 1.8f}; n.cardLayer = QUuid::createUuid();
        s.addNode(n);
        same(s, 1, 0.02, 1.5);
    }
    void backgroundOffMatchesTheCpuToo() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        Scene3D s = basicScene();
        RenderOptions o; o.drawBackground = false;
        const RenderCamera cam = sceneCamera(s, 1.5);
        const QImage cpu = renderScene(s, cam, {180, 120}, o), gpu = gpu3d::render(s, cam, {180, 120}, o);
        QVERIFY(!gpu.isNull());
        double frac, mean; metrics(cpu, gpu, 24, &frac, &mean);
        QVERIFY2(frac < 0.015 && mean < 1.2, qPrintable(QString("%1 %2").arg(frac).arg(mean)));
        QCOMPARE(qAlpha(gpu.pixel(1, 1)), 0);
    }
    void whatTheGpuCannotDoIsLeftToTheCpu() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        const RenderCamera cam = sceneCamera(basicScene(), 1.5);
        {   // depth output
            std::vector<float> d; RenderOptions o; o.distanceOut = &d;
            QVERIFY(gpu3d::render(basicScene(), cam, {100, 60}, o).isNull());
        }
        {   // a layer blend mode on a surface
            Scene3D s = basicScene(); Material m = makeMat(Qt::white); m.blendMode = 6; addMesh(s, makeCube(0.5), m, {0, 1, 1});
            QVERIFY(gpu3d::render(s, cam, {100, 60}, {}).isNull());
        }
        {   // more lights than the shader holds
            Scene3D s = basicScene(); for (int i = 0; i < 20; ++i) addLight(s, LightType::Point, {float(i), 2, 2}, {}, Qt::white, 0.1);
            QVERIFY(gpu3d::render(s, cam, {100, 60}, {}).isNull());
        }
        {   // a see-through texture on an opaque material
            Scene3D s = basicScene(); QImage t(8, 8, QImage::Format_ARGB32); t.fill(QColor(255, 0, 0, 100));
            const QUuid tid = QUuid::createUuid(); s.textures[tid] = t;
            Material m = makeMat(Qt::white); m.baseTexture = tid; addMesh(s, makeCube(0.5), m, {0, 1, 1});
            QVERIFY(gpu3d::render(s, cam, {100, 60}, {}).isNull());
        }
        {   // renderScene() then simply draws it on the CPU, with or without the GPU asked for
            Scene3D s = basicScene(); Material m = makeMat(Qt::white); m.blendMode = 6; addMesh(s, makeCube(0.5), m, {0, 1, 1});
            gpu3d::setEnabled(true);
            RenderOptions o; o.useGpu = true;
            const int n0 = gpu3d::rendersDone();
            const QImage viaFallback = renderScene(s, cam, {100, 60}, o);
            gpu3d::setEnabled(false);
            QCOMPARE(gpu3d::rendersDone(), n0);
            QCOMPARE(viaFallback, renderScene(s, cam, {100, 60}, {}));
        }
    }
    void renderSceneUsesTheGpuOnlyWhenAskedAndEnabled() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        const Scene3D s = basicScene();
        const RenderCamera cam = sceneCamera(s, 1.5);
        RenderOptions on; on.useGpu = true;
        gpu3d::setEnabled(false);
        int n = gpu3d::rendersDone();
        renderScene(s, cam, {120, 80}, on);
        QCOMPARE(gpu3d::rendersDone(), n);                       // switched off
        gpu3d::setEnabled(true);
        renderScene(s, cam, {120, 80}, {});
        QCOMPARE(gpu3d::rendersDone(), n);                       // not asked for (exports)
        renderScene(s, cam, {120, 80}, on);
        QCOMPARE(gpu3d::rendersDone(), n + 1);
        gpu3d::setEnabled(false);
    }
    void theCompositorDrawsSceneLayersOnTheGpuForPreviewsButNotWhenAskedNotTo() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        CompositionSettings cs; cs.width = 200; cs.height = 130; cs.durationFrames = 4; cs.background = Qt::white;
        Document doc(cs);
        const QUuid layer = doc.addScene3DLayer("Scene");
        doc.editScene3D(layer, [&](Scene3D& sc) { sc = basicScene(); return true; }, "Build");
        gpu3d::setEnabled(false);
        Compositor::Options cpuOpt; cpuOpt.scene3dSupersample = 1;
        const QImage cpu = Compositor::render(doc, doc.canvasRect(), cpuOpt);
        gpu3d::setEnabled(true);
        const int n0 = gpu3d::rendersDone();
        Compositor::Options noGpu = cpuOpt; noGpu.allowGpu = false;
        const QImage still = Compositor::render(doc, doc.canvasRect(), noGpu);
        QCOMPARE(gpu3d::rendersDone(), n0);                          // exports ask for this: never the GPU
        QCOMPARE(still, cpu);
        const QImage viaGpu = Compositor::render(doc, doc.canvasRect(), cpuOpt);
        QCOMPARE(gpu3d::rendersDone(), n0 + 1);
        double frac, mean; metrics(cpu, viaGpu, 24, &frac, &mean);
        QVERIFY2(frac < 0.015 && mean < 1.2, qPrintable(QString("%1 %2").arg(frac).arg(mean)));
        gpu3d::setEnabled(false);
        QCOMPARE(Compositor::render(doc, doc.canvasRect(), cpuOpt), cpu);       // and switching it off again gives the CPU picture back
    }
    void severalThreadsAndRepeatedRendersAreSafeAndStable() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        const Scene3D s = basicScene();
        const RenderCamera cam = sceneCamera(s, 1.5);
        const QImage first = gpu3d::render(s, cam, {160, 100}, {});
        QVERIFY(!first.isNull());
        QVector<QFuture<bool>> fs;
        for (int i = 0; i < 6; ++i) fs << QtConcurrent::run([&] { return gpu3d::render(s, cam, {160, 100}, {}) == first; });
        for (auto& f : fs) QVERIFY(f.result());
        // a different size afterwards re-creates the buffers
        const QImage small = gpu3d::render(s, cam, {64, 40}, {});
        QCOMPARE(small.size(), QSize(64, 40));
    }
};

QTEST_MAIN(TestGpuRender3D)
#include "test_gpu_render3d.moc"
