#include <QtTest>
#include <QPainter>

#include "TestEnv.h"
#include "Drawing/BrushEngine.h"
#include "Drawing/FloodFill.h"
#include "Scene/Document.h"

using namespace mynd;

static QImage blank(int w = 200, int h = 120) {
    QImage i(w, h, QImage::Format_ARGB32_Premultiplied);
    i.fill(Qt::transparent);
    return i;
}
static int alphaAt(const QImage& i, int x, int y) { return qAlpha(i.pixel(x, y)); }

class TestDrawing : public QObject {
    Q_OBJECT
private slots:
    void strokePaintsAlongPathOnly() {
        QImage img = blank();
        BrushSettings b; b.size = 10; b.hardness = 1; b.pressureSize = false;
        BrushStroke s(&img, b, QColor(255, 0, 0), false);
        s.begin({20, 60}); s.addPoint({100, 60}); s.end();
        QCOMPARE(alphaAt(img, 60, 60), 255);
        QCOMPARE(QColor(img.pixel(60, 60)), QColor(255, 0, 0));
        QCOMPARE(alphaAt(img, 60, 70), 0);                 // beyond the radius
        QCOMPARE(alphaAt(img, 150, 60), 0);                // beyond the end of the path
        QVERIFY(s.dirtyRect().contains(QRect(20, 55, 80, 10)));
        QVERIFY(!s.dirtyRect().contains(QPoint(150, 60)));
    }
    void strokeOpacityCapsOverlappingDabs() {
        QImage img = blank();
        BrushSettings b; b.size = 20; b.hardness = 1; b.opacity = 0.5; b.pressureSize = false; b.spacing = 0.05;
        BrushStroke s(&img, b, Qt::black, false);
        s.begin({30, 60});
        for (int i = 0; i < 4; ++i) { s.addPoint({150, 60}); s.addPoint({30, 60}); }  // scribble over the same pixels
        s.end();
        QVERIFY(qAbs(alphaAt(img, 90, 60) - 128) <= 2);   // never exceeds the stroke opacity
    }
    void flowAccumulatesWithinStroke() {
        QImage img = blank();
        BrushSettings b; b.size = 20; b.hardness = 1; b.flow = 0.2; b.opacity = 1; b.pressureSize = false; b.spacing = 0.05;
        BrushStroke s(&img, b, Qt::black, false);
        s.begin({30, 60}); s.addPoint({150, 60}); s.end();
        QVERIFY(alphaAt(img, 90, 60) > 200);   // many low-flow dabs build up to near-opaque
        QVERIFY(alphaAt(img, 90, 60) <= 255);
    }
    void softBrushFallsOffFromCenter() {
        QImage img = blank();
        BrushSettings b; b.size = 40; b.hardness = 0; b.pressureSize = false;
        BrushStroke s(&img, b, Qt::black, false);
        s.begin({100, 60}); s.end();
        QVERIFY(alphaAt(img, 100, 60) > alphaAt(img, 110, 60));
        QVERIFY(alphaAt(img, 110, 60) > alphaAt(img, 118, 60));
        QCOMPARE(alphaAt(img, 125, 60), 0);
    }
    void pressureControlsSize() {
        QImage a = blank(), c = blank();
        BrushSettings b; b.size = 30; b.hardness = 1; b.pressureSize = true;
        BrushStroke s1(&a, b, Qt::black, false); s1.begin({100, 60}, 1.0); s1.end();
        BrushStroke s2(&c, b, Qt::black, false); s2.begin({100, 60}, 0.4); s2.end();
        QCOMPARE(alphaAt(a, 100 + 12, 60), 255);
        QCOMPARE(alphaAt(c, 100 + 12, 60), 0);
        QCOMPARE(alphaAt(c, 100 + 4, 60), 255);
    }
    void pencilIsAliased() {
        QImage img = blank();
        BrushSettings b; b.size = 7; b.hardness = 1; b.antialias = false; b.pressureSize = false;
        BrushStroke s(&img, b, Qt::black, false);
        s.begin({50.5, 50.5}); s.end();
        for (int y = 0; y < img.height(); ++y) for (int x = 0; x < img.width(); ++x) {
            const int a = alphaAt(img, x, y);
            QVERIFY2(a == 0 || a == 255, "pencil must not produce partial alpha");
        }
    }
    void eraserRemovesAlpha() {
        QImage img = blank();
        img.fill(QColor(0, 0, 255));
        BrushSettings b; b.size = 20; b.hardness = 1; b.pressureSize = false;
        BrushStroke s(&img, b, Qt::black, true);
        s.begin({50, 50}); s.addPoint({150, 50}); s.end();
        QCOMPARE(alphaAt(img, 100, 50), 0);
        QCOMPARE(alphaAt(img, 100, 90), 255);
    }
    void stabilizationConvergesToFinalPoint() {
        QImage img = blank();
        BrushSettings b; b.size = 6; b.hardness = 1; b.pressureSize = false; b.stabilization = 0.9;
        BrushStroke s(&img, b, Qt::black, false);
        s.begin({20, 60});
        for (int x = 20; x <= 160; x += 10) s.addPoint({double(x), 60});
        const int midLag = alphaAt(img, 158, 60);   // before end(): brush still trailing
        s.end();
        QCOMPARE(midLag, 0);
        QVERIFY(alphaAt(img, 158, 60) > 200);
    }
    void patchUndoRedoThroughDocument() {
        CompositionSettings cs; cs.width = 100; cs.height = 80;
        Document d(cs);
        const QUuid id = d.addLayer(LayerType::Raster, "L");
        Layer* l = d.findLayer(id);
        BrushSettings b; b.size = 12; b.hardness = 1; b.pressureSize = false;
        BrushStroke s(&l->raster, b, Qt::red, false);
        s.begin({30, 40}); s.addPoint({70, 40}); s.end();
        d.commitRasterPatch(id, s.dirtyRect(), s.original(), "Brush Stroke");
        QCOMPARE(alphaAt(l->raster, 50, 40), 255);
        d.undoStack()->undo();
        QCOMPARE(alphaAt(l->raster, 50, 40), 0);
        QCOMPARE(l->raster, d.newRasterImage());
        d.undoStack()->redo();
        QCOMPARE(alphaAt(l->raster, 50, 40), 255);
        QCOMPARE(d.undoStack()->undoText(), QString("Brush Stroke"));
    }
    void floodFillStaysInsideBoundary() {
        QImage img = blank(100, 100);
        QPainter p(&img); p.setPen(QPen(Qt::black, 1)); p.setBrush(Qt::NoBrush); p.drawRect(20, 20, 50, 50); p.end();
        img.setPixelColor(5, 5, Qt::transparent);
        const QRect r = floodFill(img, {40, 40}, QColor(0, 200, 0), 0);
        QVERIFY(r.isValid());
        QCOMPARE(QColor(img.pixel(40, 40)), QColor(0, 200, 0));
        QCOMPARE(alphaAt(img, 5, 5), 0);                    // outside region untouched
        QCOMPARE(QColor(img.pixel(20, 20)), QColor(Qt::black)); // boundary untouched
        QVERIFY(QRect(21, 21, 49, 49).contains(r));
    }
    void floodFillToleranceAndNonContiguous() {
        QImage img = blank(40, 40);
        img.fill(QColor(100, 100, 100));
        img.setPixelColor(10, 10, QColor(110, 105, 100));   // near-match
        img.setPixelColor(11, 10, QColor(250, 0, 0));       // hard barrier
        img.setPixelColor(12, 10, QColor(100, 100, 100));
        QImage a = img.copy();
        floodFill(a, {0, 0}, Qt::white, 0);
        QCOMPARE(QColor(a.pixel(10, 10)), QColor(110, 105, 100));   // tolerance 0 skips it
        QImage b = img.copy();
        floodFill(b, {0, 0}, Qt::white, 12);
        QCOMPARE(QColor(b.pixel(10, 10)), QColor(Qt::white));
        QImage c = img.copy();
        QImage d = img.copy();
        // Enclose pixel (12,10)'s neighbourhood? simply compare contiguous vs global on a split canvas
        QPainter p(&c); p.fillRect(20, 0, 1, 40, QColor(250, 0, 0)); p.end();
        QPainter q(&d); q.fillRect(20, 0, 1, 40, QColor(250, 0, 0)); q.end();
        floodFill(c, {0, 0}, Qt::white, 0, true);
        floodFill(d, {0, 0}, Qt::white, 0, false);
        QCOMPARE(QColor(c.pixel(30, 30)), QColor(100, 100, 100));   // other side of the wall kept
        QCOMPARE(QColor(d.pixel(30, 30)), QColor(Qt::white));       // global fill crosses the wall
    }
};

QTEST_MAIN(TestDrawing)
#include "test_drawing.moc"
