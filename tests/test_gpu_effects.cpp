#include <QtTest>
#include <QtConcurrent>

#include "TestEnv.h"
#include "Graphics/Compositor.h"
#include "Graphics/Effects.h"
#include "Graphics/GpuEffects.h"
#include "Scene/Document.h"

using namespace mynd;

// The OpenGL effects path must give (nearly) the same pixels as the CPU path. Needs an OpenGL 3.3 context: with the "offscreen" platform
// (what CI uses) none exists and these tests skip; run them under a platform with GL (e.g. xvfb-run with QT_QPA_PLATFORM=xcb, Mesa llvmpipe).
class TestGpuEffects : public QObject {
    Q_OBJECT
    bool m_ok = false;

    static QImage sample(int w, int h) {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(220, 40, 60)); p.drawEllipse(QRectF(w * 0.15, h * 0.2, w * 0.45, h * 0.5));
        p.setBrush(QColor(40, 200, 90, 180)); p.drawRect(QRectF(w * 0.5, h * 0.4, w * 0.35, h * 0.4));
        p.setBrush(QColor(250, 250, 240)); p.drawEllipse(QRectF(w * 0.3, h * 0.55, w * 0.2, h * 0.2));
        QLinearGradient g(0, 0, w, 0); g.setColorAt(0, QColor(30, 60, 220, 255)); g.setColorAt(1, QColor(255, 220, 0, 90));
        p.setBrush(g); p.drawRect(QRectF(0, h * 0.85, w, h * 0.1));
        return img;
    }
    // largest and mean absolute channel difference
    static void diff(const QImage& a, const QImage& b, int* maxOut, double* meanOut) {
        int mx = 0; double sum = 0;
        for (int y = 0; y < a.height(); ++y) {
            const QRgb* p = reinterpret_cast<const QRgb*>(a.constScanLine(y)); const QRgb* q = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                const int d[4] = {std::abs(qRed(p[x]) - qRed(q[x])), std::abs(qGreen(p[x]) - qGreen(q[x])), std::abs(qBlue(p[x]) - qBlue(q[x])), std::abs(qAlpha(p[x]) - qAlpha(q[x]))};
                for (int v : d) { mx = std::max(mx, v); sum += v; }
            }
        }
        *maxOut = mx; *meanOut = sum / (4.0 * a.width() * a.height());
    }
    void compare(const QVector<Effect>& fxs, int maxAllowed, double meanAllowed) {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        const QImage src = sample(150, 110);
        QImage cpu = src; fx::applyEffects(cpu, fxs);
        QImage gpu = src;
        QVERIFY(fx::gpu::applyEffects(gpu, fxs));
        QCOMPARE(gpu.size(), cpu.size());
        QCOMPARE(gpu.format(), QImage::Format_ARGB32_Premultiplied);
        QVERIFY2(cpu != src, "the effect must change the picture for the comparison to mean anything");
        int mx; double mean; diff(cpu, gpu, &mx, &mean);
        QVERIFY2(mx <= maxAllowed && mean <= meanAllowed, qPrintable(QString("max diff %1, mean diff %2").arg(mx).arg(mean)));
    }

private slots:
    void initTestCase() { m_ok = fx::gpu::initialize(); if (m_ok) qInfo("GPU path: %s", qPrintable(fx::gpu::rendererName())); }
    void cleanupTestCase() { fx::gpu::shutdown(); }

    void blurMatchesTheCpu() { Effect e = Effect::make(EffectType::Blur); e.p[0] = 12; compare({e}, 3, 0.3); }
    void aLargeBlurMatchesTheCpu() { Effect e = Effect::make(EffectType::Blur); e.p[0] = 45; compare({e}, 4, 0.4); }
    void dropShadowMatchesTheCpu() { Effect e = Effect::make(EffectType::DropShadow); e.p[0] = 8; e.p[1] = 6; e.p[2] = 7; e.p[3] = 80; e.color = QColor(10, 20, 120); compare({e}, 4, 0.4); }
    void glowMatchesTheCpu() { Effect e = Effect::make(EffectType::Glow); e.p[0] = 10; e.p[1] = 150; e.p[2] = 40; compare({e}, 4, 0.4); }
    void brightnessContrastMatchesTheCpu() { Effect e = Effect::make(EffectType::BrightnessContrast); e.p[0] = 20; e.p[1] = 35; compare({e}, 3, 0.3); }
    void hueSaturationMatchesTheCpu() { Effect e = Effect::make(EffectType::HueSaturation); e.p[0] = 70; e.p[1] = 30; e.p[2] = -15; compare({e}, 4, 0.4); }
    void invertMatchesTheCpu() { compare({Effect::make(EffectType::Invert)}, 2, 0.2); }
    void aChainOfEffectsMatchesTheCpu() {
        Effect a = Effect::make(EffectType::HueSaturation); a.p[0] = 40;
        Effect b = Effect::make(EffectType::DropShadow); b.p[0] = 5; b.p[1] = 5; b.p[2] = 4; b.p[3] = 70;
        Effect c = Effect::make(EffectType::Blur); c.p[0] = 3;
        Effect d = Effect::make(EffectType::Invert); d.enabled = false;               // a disabled one is skipped on both paths
        compare({a, b, c, d}, 5, 0.5);
    }
    void imagesBiggerThanTheGpuAllowsFallBackToTheCpu() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        QImage huge(70000, 1, QImage::Format_ARGB32_Premultiplied); huge.fill(Qt::red);
        const QImage before = huge;
        QVERIFY(!fx::gpu::applyEffects(huge, {Effect::make(EffectType::Invert)}));
        QCOMPARE(huge, before);
    }
    void theCompositorUsesTheGpuWhenEnabledAndGivesTheSameCanvas() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        CompositionSettings cs; cs.width = 120; cs.height = 90; cs.durationFrames = 4; cs.background = Qt::white;
        Document doc(cs);
        const QUuid L = doc.addLayer(LayerType::Vector, "Art");
        VectorShape r = VectorShape::makeEllipse(QRectF(30, 20, 60, 50)); r.fill.color = Qt::red; r.stroke.enabled = false;
        doc.setShapes(L, {r}, "shape");
        LayerFx lf; Effect s = Effect::make(EffectType::DropShadow); s.p[0] = 6; s.p[1] = 6; s.p[2] = 5; lf.effects << s;
        Effect b = Effect::make(EffectType::Blur); b.p[0] = 2; lf.effects << b;
        doc.setLayerFx(L, lf, "fx");
        fx::gpu::setEnabled(false);
        const QImage cpu = Compositor::render(doc, doc.canvasRect(), {});
        fx::gpu::setEnabled(true);
        const QImage gpu = Compositor::render(doc, doc.canvasRect(), {});
        fx::gpu::setEnabled(false);
        int mx; double mean; diff(cpu, gpu, &mx, &mean);
        QVERIFY2(mx <= 6 && mean <= 0.6, qPrintable(QString("max %1 mean %2").arg(mx).arg(mean)));
        QVERIFY(Compositor::gpuEffectsUsed() > 0);                                       // it really went through the GPU
    }
    void callsFromSeveralThreadsAreSerialised() {
        if (!m_ok) QSKIP("no OpenGL 3.3 context on this platform");
        Effect e = Effect::make(EffectType::Blur); e.p[0] = 6;
        QImage ref = sample(90, 70); fx::applyEffects(ref, {e});
        QVector<QFuture<int>> fs;
        for (int i = 0; i < 6; ++i) fs << QtConcurrent::run([e, ref] { QImage img = sample(90, 70); if (!fx::gpu::applyEffects(img, {e})) return 255; int mx; double m; diff(img, ref, &mx, &m); return mx; });
        for (auto& f : fs) QVERIFY(f.result() <= 3);
    }
};

QTEST_MAIN(TestGpuEffects)
#include "test_gpu_effects.moc"
