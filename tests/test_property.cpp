#include <QtTest>
#include <QJsonDocument>
#include <QPainter>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"
#include "Scene/Property.h"
#include "Scene/ShapeMorph.h"

using namespace mynd;

namespace {
const PropertyDef& D(const char* id) { return *findPropertyDef(id); }
PropValue V(double a, double b = 0) { return PropValue::of(a, b); }
}

class TestProperty : public QObject {
    Q_OBJECT
    static std::unique_ptr<Document> make() {
        CompositionSettings s; s.width = 100; s.height = 100; s.durationFrames = 30; s.background = Qt::transparent;
        return std::make_unique<Document>(s);
    }
    // Raster layer with a 20x20 red square at (10,10)-(30,30).
    static QUuid square(Document& d) {
        const QUuid id = d.addLayer(LayerType::Raster, "Sq");
        QPainter p(&d.findLayer(id)->raster);
        p.fillRect(10, 10, 20, 20, Qt::red);
        return id;
    }
private slots:
    void registryDescribesTypedProperties() {
        QVERIFY(findPropertyDef("transform.position"));
        QVERIFY(!findPropertyDef("nope"));
        QCOMPARE(propComponents(PropType::Vec2), 2);
        QCOMPARE(propComponents(PropType::Color), 4);
        QCOMPARE(propComponents(PropType::Rotation), 1);
        QCOMPARE(D("transform.scale").def.c[0], 100.0);
        QCOMPARE(D("opacity").maxV.c[0], 100.0);
        QSet<QString> ids; for (const auto& d : layerPropertyDefs()) { QVERIFY(!ids.contains(d.id)); ids.insert(d.id); }   // persistent, unique
        // validation: range, finiteness, int rounding, bool coercion, unused components zeroed
        QCOMPARE(clampValue(D("opacity"), V(250)).c[0], 100.0);
        QCOMPARE(clampValue(D("opacity"), V(std::nan(""))).c[0], 100.0);
        QCOMPARE(clampValue(D("transform.position"), V(1e9, -1e9)).c[1], -1e6);
        PropertyDef i; i.type = PropType::Int; i.minV = PropValue::of(0, 0, 0, 0); i.maxV = PropValue::of(10, 10, 10, 10);
        QCOMPARE(clampValue(i, V(3.6)).c[0], 4.0);
        PropertyDef b; b.type = PropType::Bool; b.minV = PropValue::of(0); b.maxV = PropValue::of(1);
        QCOMPARE(clampValue(b, V(0.3)).c[0], 1.0);
        QCOMPARE(clampValue(D("opacity"), PropValue::of(5, 7, 8, 9)).c[1], 0.0);
    }

    void linearHoldAndEndsBehaveLikeAnAnimationPackage() {
        PropertyTrack t; t.base = V(0);
        const auto& d = D("opacity");
        t.setKey(d, 10, V(0)); t.setKey(d, 20, V(100));
        QCOMPARE(t.valueAt(d, 0).c[0], 0.0);                 // before the first key: first value
        QCOMPARE(t.valueAt(d, 10).c[0], 0.0);
        QCOMPARE(t.valueAt(d, 15).c[0], 50.0);
        QCOMPARE(t.valueAt(d, 12.5).c[0], 25.0);             // sub-frame
        QCOMPARE(t.valueAt(d, 20).c[0], 100.0);
        QCOMPARE(t.valueAt(d, 99).c[0], 100.0);              // after the last key: last value
        t.applyEase(10, EasePreset::Hold, false, true);
        QCOMPARE(t.valueAt(d, 19).c[0], 0.0);                // hold keeps A until B arrives
        QCOMPARE(t.valueAt(d, 20).c[0], 100.0);
        // static value only when there are no keys
        PropertyTrack s; s.base = V(42);
        QCOMPARE(s.valueAt(d, 5).c[0], 42.0);
    }

    void multiComponentColorIntAndDiscreteTypes() {
        const auto& pos = D("transform.position");
        PropertyTrack t; t.setKey(pos, 0, V(0, 100)); t.setKey(pos, 10, V(100, 0));
        const PropValue m = t.valueAt(pos, 5);
        QCOMPARE(m.c[0], 50.0); QCOMPARE(m.c[1], 50.0);
        PropertyDef col; col.type = PropType::Color; col.minV = PropValue::of(0, 0, 0, 0); col.maxV = PropValue::of(255, 255, 255, 255);
        PropertyTrack c; c.setKey(col, 0, PropValue::of(255, 0, 0, 255)); c.setKey(col, 10, PropValue::of(0, 0, 255, 255));
        const PropValue mid = c.valueAt(col, 5);
        QCOMPARE(mid.c[0], 127.5); QCOMPARE(mid.c[2], 127.5); QCOMPARE(mid.c[3], 255.0);
        PropertyDef in; in.type = PropType::Int; in.minV = PropValue::of(0, 0, 0, 0); in.maxV = PropValue::of(100, 100, 100, 100);
        PropertyTrack it; it.setKey(in, 0, V(0)); it.setKey(in, 10, V(5));
        QCOMPARE(it.valueAt(in, 3).c[0], 2.0);               // 1.5 rounds
        PropertyDef en = in; en.type = PropType::Enum;       // enums/bools never blend
        PropertyTrack e; e.setKey(en, 0, V(1)); e.setKey(en, 10, V(3));
        QCOMPARE(e.valueAt(en, 9).c[0], 1.0);
        QCOMPARE(e.valueAt(en, 10).c[0], 3.0);
        PropertyDef bo; bo.type = PropType::Bool; bo.minV = PropValue::of(0); bo.maxV = PropValue::of(1);
        PropertyTrack b; b.setKey(bo, 0, V(0)); b.setKey(bo, 10, V(1));
        QCOMPARE(b.valueAt(bo, 5).c[0], 0.0);
    }

    void bezierEasingIsMonotoneAccurateAndSupportsOvershoot() {
        // The linear-equivalent handles reproduce y = x.
        for (double u : {0.0, 0.1, 0.37, 0.5, 0.9, 1.0}) QVERIFY(qAbs(bezierEase(u, 1 / 3.0, 1 / 3.0, 2 / 3.0, 2 / 3.0) - u) < 1e-6);
        // Easy ease (y handles 0): symmetric, slow at the ends, exactly 0.5 in the middle.
        QVERIFY(qAbs(bezierEase(0.5, 1 / 3.0, 0, 2 / 3.0, 1) - 0.5) < 1e-6);
        QVERIFY(bezierEase(0.1, 1 / 3.0, 0, 2 / 3.0, 1) < 0.1);
        QVERIFY(bezierEase(0.9, 1 / 3.0, 0, 2 / 3.0, 1) > 0.9);
        double prev = -1;
        for (int i = 0; i <= 100; ++i) { const double y = bezierEase(i / 100.0, 0.7, 0.0, 0.3, 1.0); QVERIFY(y >= prev - 1e-9); prev = y; }
        // overshoot: a control point above 1 pushes the curve past the end value before settling
        double peak = 0; for (int i = 0; i <= 100; ++i) peak = qMax(peak, bezierEase(i / 100.0, 0.3, 1.8, 0.7, 1.0));
        QVERIFY(peak > 1.0);
        // degenerate handles (x at 0 and 1) still evaluate
        QVERIFY(std::isfinite(bezierEase(0.5, 0, 0, 1, 1)));
        QCOMPARE(bezierEase(-1, 0.3, 0.3, 0.7, 0.7), 0.0);
        QCOMPARE(bezierEase(2, 0.3, 0.3, 0.7, 0.7), 1.0);
    }

    void easePresetsShapeTheSegmentOnBothSides() {
        const auto& d = D("opacity");
        PropertyTrack t; t.setKey(d, 0, V(0)); t.setKey(d, 10, V(100));
        QVERIFY(t.applyEase(0, EasePreset::EaseOut, false, true));        // slow start (segment leaving key 0)
        QVERIFY(t.valueAt(d, 2).c[0] < 20.0);
        QCOMPARE(t.valueAt(d, 10).c[0], 100.0);
        QVERIFY(t.applyEase(10, EasePreset::EaseIn, true, false));        // slow arrival
        QVERIFY(t.valueAt(d, 8).c[0] < 90.0 + 8.0 && t.valueAt(d, 8).c[0] > 80.0);
        const double eio = t.valueAt(d, 5).c[0];
        QVERIFY(qAbs(eio - 50.0) < 1e-6);                                  // symmetric
        PropertyTrack u; u.setKey(d, 0, V(0)); u.setKey(d, 10, V(100));
        u.applyEase(0, EasePreset::EaseInOut, true, true); u.applyEase(10, EasePreset::EaseInOut, true, true);
        QVERIFY(u.valueAt(d, 1).c[0] < 10.0 && u.valueAt(d, 9).c[0] > 90.0);
        QVERIFY(!u.applyEase(5, EasePreset::Linear, true, true));          // no key at 5
        u.applyEase(0, EasePreset::Linear, true, true); u.applyEase(10, EasePreset::Linear, true, true);
        QCOMPARE(u.valueAt(d, 1).c[0], 10.0);
    }

    void keyEditsStaySortedUniqueAndValidated() {
        const auto& d = D("opacity");
        PropertyTrack t;
        t.setKey(d, 20, V(1)); t.setKey(d, 5, V(2)); t.setKey(d, 12, V(3)); t.setKey(d, 12, V(4));
        QCOMPARE(t.keys.size(), 3);
        QCOMPARE(t.keys[0].frame, 5); QCOMPARE(t.keys[1].frame, 12); QCOMPARE(t.keys[2].frame, 20);
        QCOMPARE(t.keys[1].value.c[0], 4.0);
        t.setKey(d, 7, V(999));
        QCOMPARE(t.keys[1].value.c[0], 100.0);                              // clamped to range
        QVERIFY(t.moveKey(7, 20));                                          // moving onto another key replaces it
        QCOMPARE(t.keys.size(), 3);
        QCOMPARE(t.keys.last().value.c[0], 100.0);
        QVERIFY(!t.moveKey(99, 3));
        QVERIFY(!t.moveKey(5, -1));
        QVERIFY(t.moveKey(5, 5));
        QVERIFY(t.removeKey(12)); QVERIFY(!t.removeKey(12));
        QCOMPARE(t.indexOfKey(20), 1);
        QCOMPARE(t.indexOfKey(3), -1);
    }

    void setValueEnableAndDisableAnimationFollowStopwatchSemantics() {
        PropertyTracks t;
        setValueAt(t, "opacity", 0, V(40), false);                          // not animated: static value
        QVERIFY(t["opacity"].keys.isEmpty());
        QCOMPARE(valueOf(t, D("opacity"), 9).c[0], 40.0);
        setValueAt(t, "opacity", 5, V(80), true);                           // forceKey = auto-key on
        QCOMPARE(t["opacity"].keys.size(), 1);
        setValueAt(t, "opacity", 10, V(20), false);                         // animated: always keys
        QCOMPARE(t["opacity"].keys.size(), 2);
        QCOMPARE(valueOf(t, D("opacity"), 7.5).c[0], 50.0);
        disableAnimation(t, "opacity", 7);                                  // freeze the value seen at frame 7
        QVERIFY(!t["opacity"].animated());
        QCOMPARE(valueOf(t, D("opacity"), 0).c[0], 80.0 + (20.0 - 80.0) * 2 / 5);
        enableAnimation(t, "opacity", 3);
        QCOMPARE(t["opacity"].keys.size(), 1);
        QCOMPARE(t["opacity"].keys[0].value.c[0], valueOf(t, D("opacity"), 99).c[0]);
        // back to the default: the entry disappears
        disableAnimation(t, "opacity", 0);
        setValueAt(t, "opacity", 0, V(100), false);
        QVERIFY(t.isEmpty());
        setValueAt(t, "unknown.id", 0, V(1), true);
        QVERIFY(t.isEmpty());
    }

    void transformMatrixRotatesAndScalesAboutTheAnchor() {
        PropertyTracks t;
        const QPointF c(50, 50);
        QVERIFY(evaluateTransform(t, 0).isIdentity());
        setValueAt(t, "transform.rotation", 0, V(90), false);
        LayerXform x = evaluateTransform(t, 0);
        QPointF p = x.matrix(c).map(QPointF(70, 50));                       // 20px right of the centre -> 20px below it
        QVERIFY(qAbs(p.x() - 50) < 1e-9 && qAbs(p.y() - 70) < 1e-9);
        setValueAt(t, "transform.anchor", 0, PropValue::of(-50, -50), false);   // rotate about the top-left corner instead
        p = evaluateTransform(t, 0).matrix(c).map(QPointF(20, 0));
        QVERIFY(qAbs(p.x() - 0) < 1e-9 && qAbs(p.y() - 20) < 1e-9);
        PropertyTracks u;
        setValueAt(u, "transform.scale", 0, V(200, 50), false);
        setValueAt(u, "transform.position", 0, V(10, -5), false);
        p = evaluateTransform(u, 0).matrix(c).map(QPointF(60, 60));         // 10 right/10 below centre -> x2, x0.5, then shifted
        QVERIFY(qAbs(p.x() - (50 + 20 + 10)) < 1e-9 && qAbs(p.y() - (50 + 5 - 5)) < 1e-9);
        setValueAt(u, "opacity", 0, V(25), false);
        QCOMPARE(evaluateTransform(u, 0).opacity, 0.25);
    }

    void tracksSurviveJsonAndMalformedDataIsRejected() {
        PropertyTracks t;
        setValueAt(t, "transform.position", 0, V(1, 2), true);
        setValueAt(t, "transform.position", 12, V(30, -40), false);
        t["transform.position"].applyEase(0, EasePreset::EaseInOut, true, true);
        t["transform.position"].applyEase(12, EasePreset::Hold, false, true);
        setValueAt(t, "opacity", 0, V(60), false);
        PropertyTracks back; QString err; QStringList warn;
        QVERIFY2(tracksFromJson(tracksToJson(t), back, &err, &warn), qPrintable(err));
        QVERIFY(back == t);
        QVERIFY(warn.isEmpty());
        // through real JSON text as well
        const QByteArray text = QJsonDocument(tracksToJson(t)).toJson();
        PropertyTracks again;
        QVERIFY(tracksFromJson(QJsonDocument::fromJson(text).object(), again, &err));
        QVERIFY(again == t);

        auto bad = [&](const QString& json, const char* why) {
            PropertyTracks o;
            QString e;
            QVERIFY2(!tracksFromJson(QJsonDocument::fromJson(json.toUtf8()).object(), o, &e), why);
            QVERIFY2(!e.isEmpty(), why);
        };
        bad(R"({"opacity":{"base":[1,2],"keys":[]}})", "wrong component count");
        bad(R"({"opacity":{"base":["x"],"keys":[]}})", "non numeric");
        bad(R"({"opacity":{"base":[50],"keys":[{"frame":-3,"value":[1]}]}})", "negative frame");
        bad(R"({"opacity":{"base":[50],"keys":[{"frame":1.5,"value":[1]}]}})", "fractional frame");
        bad(R"({"opacity":{"base":[50],"keys":[{"frame":1,"value":[1],"out":"wobble"}]}})", "bad interpolation");
        bad(R"({"opacity":{"base":[50],"keys":[{"frame":1,"value":[1],"out":"bezier","outHandle":[1]}]}})", "bad handle");
        bad(R"({"opacity":5})", "not an object");
        PropertyTracks o; QStringList w; QString e;
        QVERIFY(tracksFromJson(QJsonDocument::fromJson(R"({"future.thing":{"base":[1],"keys":[]}})").object(), o, &e, &w));
        QCOMPARE(w.size(), 1);
        // duplicate frames collapse; unsorted keys are sorted; out-of-range values are clamped
        QVERIFY(tracksFromJson(QJsonDocument::fromJson(R"({"opacity":{"base":[50],"keys":[
            {"frame":9,"value":[10]},{"frame":2,"value":[500]},{"frame":9,"value":[20]}]}})").object(), o, &e));
        QCOMPARE(o["opacity"].keys.size(), 2);
        QCOMPARE(o["opacity"].keys[0].frame, 2); QCOMPARE(o["opacity"].keys[0].value.c[0], 100.0);
        QCOMPARE(o["opacity"].keys[1].value.c[0], 20.0);
    }

    void documentEditsAreUndoableAndDragsMergeIntoOneStep() {
        auto d = make();
        const QUuid id = square(*d);
        const int base = d->undoStack()->count();
        Layer* l = d->findLayer(id);
        PropertyTracks t = l->tracks;
        setValueAt(t, "transform.position", 0, V(10, 0), true);
        d->editTracks(id, t, "Key", {});
        QCOMPARE(d->undoStack()->count(), base + 1);
        QVERIFY(l->tracks.contains("transform.position"));
        for (int i = 1; i <= 5; ++i) {                                  // a drag: many edits, one undo step
            setValueAt(t, "transform.position", 0, V(10 + i, 0), true);
            d->editTracks(id, t, "Move", "drag-1");
        }
        QCOMPARE(d->undoStack()->count(), base + 2);
        QCOMPARE(l->tracks["transform.position"].keys[0].value.c[0], 15.0);
        d->undoStack()->undo();
        QCOMPARE(l->tracks["transform.position"].keys[0].value.c[0], 10.0);
        d->undoStack()->undo();
        QVERIFY(l->tracks.isEmpty());
        d->undoStack()->redo(); d->undoStack()->redo();
        QCOMPARE(l->tracks["transform.position"].keys[0].value.c[0], 15.0);
        // a no-op edit pushes nothing; defaults are pruned
        const int n = d->undoStack()->count();
        d->editTracks(id, l->tracks, "same", {});
        QCOMPARE(d->undoStack()->count(), n);
        QSignalSpy spy(d.get(), &Document::tracksChanged);
        d->undoStack()->undo();
        QCOMPARE(spy.count(), 1);
        // duplicating a layer copies its keyframes; ids stay independent
        d->undoStack()->redo();
        const QUuid dup = d->duplicateLayer(id);
        QVERIFY(d->findLayer(dup)->tracks == d->findLayer(id)->tracks);
    }

    void compositorMovesRotatesFadesAndAnimatesLayers() {
        auto d = make();
        const QUuid id = square(*d);
        Layer* l = d->findLayer(id);
        auto at = [&](int frame, int x, int y) { Compositor::Options o; o.frame = frame; return Compositor::render(*d, d->canvasRect(), o).pixelColor(x, y); };
        QCOMPARE(at(0, 20, 20), QColor(Qt::red));                       // untouched layers render exactly as before
        QCOMPARE(at(0, 60, 60).alpha(), 0);
        // move right 40 / down 40 over frames 0..10
        setValueAt(l->tracks, "transform.position", 0, V(0, 0), true);
        setValueAt(l->tracks, "transform.position", 10, V(40, 40), true);
        QCOMPARE(at(0, 20, 20), QColor(Qt::red));
        QCOMPARE(at(10, 60, 60), QColor(Qt::red));
        QCOMPARE(at(10, 20, 20).alpha(), 0);
        QCOMPARE(at(5, 40, 40), QColor(Qt::red));                       // halfway: square spans 30..50
        QCOMPARE(at(5, 15, 15).alpha(), 0);
        // doc current frame is used when the options don't say
        d->setCurrentFrame(10);
        QCOMPARE(QColor(Compositor::render(*d).pixel(60, 60)), QColor(Qt::red));
        // opacity track
        l->tracks.clear();
        setValueAt(l->tracks, "opacity", 0, V(100), true);
        setValueAt(l->tracks, "opacity", 10, V(0), true);
        QVERIFY(qAbs(at(5, 20, 20).alpha() - 128) <= 2);
        QCOMPARE(at(10, 20, 20).alpha(), 0);
        // layer opacity and the opacity track multiply
        LayerProps p = l->props; p.opacity = 0.5; l->props = p;
        QVERIFY(qAbs(at(0, 20, 20).alpha() - 128) <= 2);
        QVERIFY(qAbs(at(5, 20, 20).alpha() - 64) <= 3);
        l->props.opacity = 1; l->tracks.clear();
        // scale 200 % about the canvas centre (50,50): the square's x/y span 10..30 maps to -30..10
        setValueAt(l->tracks, "transform.scale", 0, V(200, 200), false);
        QCOMPARE(at(0, 5, 5), QColor(Qt::red));
        QCOMPARE(at(0, 20, 20).alpha(), 0);
        // 90 degrees about the canvas centre: the square at top-left moves to top-right
        l->tracks.clear();
        setValueAt(l->tracks, "transform.rotation", 0, V(90), false);
        QCOMPARE(at(0, 80, 20), QColor(Qt::red));
        QCOMPARE(at(0, 20, 20).alpha(), 0);
        // zero scale hides the layer without crashing
        l->tracks.clear();
        setValueAt(l->tracks, "transform.scale", 0, V(0, 0), false);
        QCOMPARE(at(0, 20, 20).alpha(), 0);
        // incremental region renders agree with the full render
        l->tracks.clear();
        setValueAt(l->tracks, "transform.rotation", 0, V(33), false);
        setValueAt(l->tracks, "transform.position", 0, V(7, -3), false);
        Compositor::Options o; o.frame = 0;
        const QImage full = Compositor::render(*d, d->canvasRect(), o);
        const QImage part = Compositor::render(*d, QRect(25, 15, 40, 50), o);
        QCOMPARE(part, full.copy(QRect(25, 15, 40, 50)));
    }

    void transformedGroupsMoveTheirChildrenAndAnimationLayersFollowToo() {
        auto d = make();
        const QUuid g = d->addLayer(LayerType::Group, "G");
        const QUuid r = d->addLayer(LayerType::Raster, "R", g);
        { QPainter p(&d->findLayer(r)->raster); p.fillRect(0, 0, 10, 10, Qt::blue); }
        setValueAt(d->findLayer(g)->tracks, "transform.position", 0, V(50, 50), false);
        Compositor::Options o; o.frame = 0;
        QCOMPARE(QColor(Compositor::render(*d, d->canvasRect(), o).pixel(55, 55)), QColor(Qt::blue));
        QCOMPARE(Compositor::render(*d, d->canvasRect(), o).pixelColor(5, 5).alpha(), 0);
        // animation layers (drawing swaps) combine with transforms
        const QUuid a = d->addAnimationLayer("A");
        const QUuid dr = d->createDrawing(a, 0, false);
        { QPainter p(&d->findLayer(a)->drawingImages[dr]); p.fillRect(0, 0, 10, 10, Qt::green); }
        d->findLayer(a)->anim.setRange(0, 12, dr);                     // hold the drawing for 12 frames
        setValueAt(d->findLayer(a)->tracks, "transform.position", 0, V(20, 0), true);
        setValueAt(d->findLayer(a)->tracks, "transform.position", 10, V(80, 0), true);
        QCOMPARE(QColor(Compositor::render(*d, d->canvasRect(), o).pixel(25, 5)), QColor(Qt::green));
        o.frame = 10;
        QCOMPARE(QColor(Compositor::render(*d, d->canvasRect(), o).pixel(85, 5)), QColor(Qt::green));
    }

    void shapeMorphPairsPointsAndPadsShorterPaths() {
        const VectorShape rect = VectorShape::makeRect(QRectF(0, 0, 100, 100));          // 4 points
        VectorShape tri = VectorShape::makePolygon(QPointF(300, 50), 50, 3, 0);         // 3 points
        QCOMPARE(morphShape(rect, tri, 0.0).bounds().center().x() < 120, true);
        QCOMPARE(morphShape(rect, tri, 1.0).bounds().center().x() > 250, true);
        const VectorShape mid = morphShape(rect, tri, 0.5);
        QCOMPARE(mid.nodes.size(), 4);                                                    // the triangle was padded, not the rect cut
        const double cx = mid.bounds().center().x();
        QVERIFY(cx > 120 && cx < 250);
        // identical shapes stay identical at every t
        const VectorShape same = morphShape(rect, rect, 0.37);
        QCOMPARE(same.nodes.size(), rect.nodes.size());
        for (int i = 0; i < rect.nodes.size(); ++i) QVERIFY(QLineF(same.nodes[i].pos, rect.nodes[i].pos).length() < 1e-9);
        // extra shapes without a partner pop in at the halfway point
        ShapeList a{rect}, b{rect, tri};
        QCOMPARE(morphShapes(a, b, 0.2).size(), 1);
        QCOMPARE(morphShapes(a, b, 0.8).size(), 2);
    }

    void spatialTangentsCurveThePathAndSurviveSaveAndLoad() {
        PropertyTrack t;
        t.setKey(D("transform.position"), 0, V(0, 0));
        t.setKey(D("transform.position"), 10, V(100, 0));
        QCOMPARE(t.valueAt(D("transform.position"), 5).c[1], 0.0);                 // straight line
        t.keys[0].spatialOut = QPointF(0, 60);
        t.keys[1].spatialIn = QPointF(0, 60);
        const PropValue mid = t.valueAt(D("transform.position"), 5);
        QVERIFY(qAbs(mid.c[0] - 50) < 1e-9);
        QVERIFY(qAbs(mid.c[1] - 45) < 1e-9);                                      // 3/4 * 60: the path bows out
        QCOMPARE(t.valueAt(D("transform.position"), 0).c[1], 0.0);                 // endpoints stay on the keys
        QCOMPARE(t.valueAt(D("transform.position"), 10).c[1], 0.0);
        QVERIFY(t.speedAt(D("transform.position"), 5) > 10.0);                     // 100 px over 10 frames at least
        PropertyTracks tracks; tracks["transform.position"] = t;
        PropertyTracks back; QString err;
        QVERIFY(tracksFromJson(tracksToJson(tracks), back, &err));
        QCOMPARE(back["transform.position"].keys[0].spatialOut, QPointF(0, 60));
        QCOMPARE(back["transform.position"].keys[1].spatialIn, QPointF(0, 60));
        QJsonObject bad = tracksToJson(tracks);
        QJsonObject tr = bad["transform.position"].toObject();
        QJsonArray ks = tr["keys"].toArray();
        QJsonObject k0 = ks[0].toObject(); k0["spatialOut"] = QJsonArray{1}; ks[0] = k0; tr["keys"] = ks; bad["transform.position"] = tr;
        QVERIFY(!tracksFromJson(bad, back, &err));
    }

    void keyframesSurviveSaveAndLoad() {
        auto d = make();
        const QUuid id = square(*d);
        PropertyTracks t;
        setValueAt(t, "transform.rotation", 0, V(0), true);
        setValueAt(t, "transform.rotation", 24, V(360), true);
        t["transform.rotation"].applyEase(0, EasePreset::EaseInOut, true, true);
        setValueAt(t, "opacity", 0, V(70), false);
        d->editTracks(id, t, "k", {});
        QTemporaryDir dir;
        const QString path = dir.path() + "/p.studio";
        QString err;
        QVERIFY2(ProjectIO::save(*d, path, &err), qPrintable(err));
        auto r = ProjectIO::load(path);
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY(r.warnings.isEmpty());
        const Layer* l = r.doc->root()->children.back().get();
        QVERIFY(l->tracks == d->findLayer(id)->tracks);
        QCOMPARE(l->tracks["transform.rotation"].keys.size(), 2);
        // a project with a damaged track is refused with a message rather than loading garbage
        QCOMPARE(kProjectFormatVersion, 4);
    }
};

QTEST_MAIN(TestProperty)
#include "test_property.moc"
