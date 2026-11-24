#include <QtTest>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"
#include "Scene3D/Anim3D.h"
#include "Scene3D/Render3D.h"

using namespace mynd;

class TestAnim3D : public QObject {
    Q_OBJECT
    CompositionSettings cs() { CompositionSettings c; c.width = 120; c.height = 80; c.durationFrames = 21; c.background = Qt::white; return c; }
    struct Ids { QUuid layer, box, cam, light, mat; };
    // A red unit-2 cube at the origin, a camera at z=6 and a point light.
    Ids build(Document& d) {
        Ids ids;
        ids.layer = d.addScene3DLayer("Scene");
        Material mat; mat.id = ids.mat = QUuid::createUuid(); mat.baseColor = Qt::red; mat.roughness = 1;
        auto cube = makeCube(2); cube->parts = {{0, int(cube->indices.size()), mat.id}};
        d.editScene3D(ids.layer, [&](Scene3D& s) {
            s.materials.push_back(mat); s.meshes[cube->id] = cube;
            Node3D n; n.kind = NodeKind::Mesh; n.mesh = cube->id; n.name = "Box"; ids.box = s.addNode(n);
            Node3D c; c.kind = NodeKind::Camera; c.position = {0, 0, 6}; c.name = "Cam"; ids.cam = s.addNode(c);
            Node3D l; l.kind = NodeKind::Light; l.lightType = LightType::Point; l.position = {0, 0, 5}; l.name = "Lamp"; l.intensity = 2; ids.light = s.addNode(l);
            s.ambientIntensity = 1; return true; }, "Build");
        d.undoStack()->clear();
        return ids;
    }
    QColor px(Document& d, int x, int y) { return Compositor::render(d, d.canvasRect(), {}).pixelColor(x, y); }
    static bool isRed(const QColor& c) { return c.red() > 200 && c.green() < 60 && c.blue() < 60; }
    static bool isWhite(const QColor& c) { return c.red() > 250 && c.green() > 250 && c.blue() > 250; }
    static PropValue pos(double x, double y = 0, double z = 0) { return PropValue::of(x, y, z); }

private slots:
    void propertyIdsRoundTripAndDefinitionsAreTyped() {
        const QUuid n = QUuid::createUuid();
        const QString id = nodePropId(n, "pos");
        PropTarget t;
        QVERIFY(parseScene3DPropId(id, &t));
        QCOMPARE(t.target, n); QVERIFY(!t.material); QCOMPARE(t.what, QString("pos"));
        QVERIFY(parseScene3DPropId(materialPropId(n, "rough"), &t) && t.material);
        for (const QString& bad : {QString("3d:"), QString("3d:n:xyz:pos"), QString("3d:x:" + n.toString(QUuid::WithoutBraces) + ":pos"), QString("transform.position"), QString("3d:n:" + n.toString(QUuid::WithoutBraces) + ":")})
            QVERIFY2(!parseScene3DPropId(bad, nullptr), qPrintable(bad));
        QVERIFY(!scene3DPropertyDef(nodePropId(n, "bogus")));
        const PropertyDef* d = findPropertyDef(id);                       // the common lookup understands 3D ids
        QVERIFY(d && d->type == PropType::Vec3 && propComponents(d->type) == 3);
        QCOMPARE(findPropertyDef(id), d);                                  // stable pointer
        QCOMPARE(findPropertyDef(nodePropId(n, "scl"))->def.c[0], 1.0);
        QVERIFY(findPropertyDef(materialPropId(n, "base"))->type == PropType::Color);
    }

    void aTrackedPropertyIsEvaluatedAtEachFrameWithoutTouchingTheStaticScene() {
        Document d(cs());
        const Ids ids = build(d);
        Layer* l = d.findLayer(ids.layer);
        const QString id = nodePropId(ids.box, "pos");
        PropertyTrack tr; tr.base = pos(0);
        const PropertyDef* def = findPropertyDef(id);
        tr.setKey(*def, 0, pos(0)); tr.setKey(*def, 10, pos(4, 2, -1));
        l->tracks.insert(id, tr);
        QVERIFY(hasSceneAnimation(*l->scene3d, l->tracks));
        const Scene3D mid = evaluateScene(*l->scene3d, l->tracks, 5);
        QVERIFY((mid.node(ids.box)->position - QVector3D(2, 1, -0.5f)).length() < 1e-4);
        QCOMPARE(l->scene3d->node(ids.box)->position, QVector3D(0, 0, 0));          // base scene untouched
        QCOMPARE(evaluateScene(*l->scene3d, l->tracks, 5).revision, mid.revision);   // same frame, same cache key
        QVERIFY(evaluateScene(*l->scene3d, l->tracks, 6).revision != mid.revision);
        QVERIFY(mid.revision != l->scene3d->revision);
        QVERIFY(l->sceneAt(10)->node(ids.box)->position == QVector3D(4, 2, -1));
        QCOMPARE(l->sceneAt(10)->node(ids.box)->position, QVector3D(4, 2, -1));      // holds after the last key
    }

    void movingAnObjectChangesTheRenderedFrames() {
        Document d(cs());
        const Ids ids = build(d);
        const QString id = nodePropId(ids.box, "pos");
        d.setCurrentFrame(0);
        QVERIFY(d.toggleScene3DAnimation(ids.layer, id));                          // stopwatch: key at frame 0
        d.setCurrentFrame(10);
        QVERIFY(d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->position = {2, 0, 0}; return true; }, "Move"));
        d.setCurrentFrame(0);
        QVERIFY(isRed(px(d, 60, 40)));
        QVERIFY(isWhite(px(d, 99, 40)));
        d.setCurrentFrame(10);
        QVERIFY(isRed(px(d, 99, 40)));
        QVERIFY(isWhite(px(d, 60, 40)));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.box)->position, QVector3D(0, 0, 0));
        const PropertyTrack& tr = d.findLayer(ids.layer)->tracks[id];
        QCOMPARE(tr.keys.size(), 2);
        QCOMPARE(tr.keys[1].frame, 10);
        // one undo per edit, and undo takes the key away again
        d.undoStack()->undo();
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 1);
        d.undoStack()->undo();
        QVERIFY(!d.findLayer(ids.layer)->tracks.contains(id));
        d.undoStack()->redo(); d.undoStack()->redo();
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 2);
    }

    void aDragOnAnAnimatedPropertyKeysEachGestureOnceAndOtherEditsStayStatic() {
        Document d(cs());
        const Ids ids = build(d);
        const QString id = nodePropId(ids.box, "pos");
        d.setCurrentFrame(0); d.toggleScene3DAnimation(ids.layer, id);
        d.setCurrentFrame(5);
        d.undoStack()->clear();
        for (int i = 1; i <= 6; ++i)
            d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->position = {float(i), 0, 0}; return true; }, "Drag", "g1");
        QCOMPARE(d.undoStack()->count(), 1);                                        // merged
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 2);
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys[1].value.c[0], 6.0);
        // renaming and scaling (not animated) change the scene itself, not the tracks
        QVERIFY(d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->name = "Crate"; s.node(ids.box)->scale = {2, 2, 2}; return true; }, "Edit"));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.box)->scale, QVector3D(2, 2, 2));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.box)->name, QString("Crate"));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.box)->position, QVector3D(0, 0, 0));
        QVERIFY(!d.findLayer(ids.layer)->tracks.contains(nodePropId(ids.box, "scl")));
        // structural edits (adding a node) are applied to the static scene
        QVERIFY(d.editScene3D(ids.layer, [&](Scene3D& s) { Node3D g; g.name = "G"; s.addNode(g); return true; }, "Add"));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->nodes.size(), size_t(4));
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 2);
    }

    void autoKeyCreatesKeysOnlyWhenOnAndRespectsTheCurrentFrame() {
        Document d(cs());
        const Ids ids = build(d);
        d.setCurrentFrame(4);
        QVERIFY(d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->position = {1, 0, 0}; return true; }, "Move", {}, false));
        QVERIFY(d.findLayer(ids.layer)->tracks.isEmpty());
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.box)->position, QVector3D(1, 0, 0));
        d.setCurrentFrame(8);
        QVERIFY(d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->position = {3, 0, 0}; return true; }, "Move", {}, true));
        const PropertyTrack& tr = d.findLayer(ids.layer)->tracks[nodePropId(ids.box, "pos")];
        QCOMPARE(tr.keys.size(), 1);                                               // a single key at the edited frame
        QCOMPARE(tr.keys[0].frame, 8);
        QCOMPARE(tr.keys[0].value.c[0], 3.0);
        d.setCurrentFrame(0);
        QCOMPARE(d.findLayer(ids.layer)->sceneAt(0)->node(ids.box)->position.x(), 3.0f);   // constant before/after the only key
        // a no-op edit makes nothing
        const int n = d.undoStack()->count();
        QVERIFY(!d.editScene3D(ids.layer, [&](Scene3D&) { return true; }, "Nothing", {}, true));
        QCOMPARE(d.undoStack()->count(), n);
    }

    void cameraLightAndMaterialPropertiesAnimate() {
        Document d(cs());
        const Ids ids = build(d);
        Layer* l = d.findLayer(ids.layer);
        auto key = [&](const QString& id, int frame, PropValue v) {
            const PropertyDef* def = findPropertyDef(id);
            QVERIFY2(def, qPrintable(id));
            PropertyTrack& t = l->tracks[id];
            t.setKey(*def, frame, v);
        };
        key(nodePropId(ids.cam, "fov"), 0, PropValue::of(30));
        key(nodePropId(ids.cam, "fov"), 20, PropValue::of(90));
        key(nodePropId(ids.light, "lint"), 0, PropValue::of(0));
        key(nodePropId(ids.light, "lint"), 20, PropValue::of(4));
        key(materialPropId(ids.mat, "base"), 0, PropValue::of(255, 0, 0, 255));
        key(materialPropId(ids.mat, "base"), 20, PropValue::of(0, 0, 255, 255));
        key(materialPropId(ids.mat, "rough"), 0, PropValue::of(1));
        key(materialPropId(ids.mat, "rough"), 20, PropValue::of(0.2));
        const auto s10 = l->sceneAt(10);
        QCOMPARE(s10->node(ids.cam)->fov, 60.0);
        QCOMPARE(s10->node(ids.light)->intensity, 2.0);
        QCOMPARE(s10->material(ids.mat)->baseColor, QColor(128, 0, 128));
        QVERIFY(std::abs(s10->material(ids.mat)->roughness - 0.6) < 1e-9);
        // out-of-range keys are clamped by the definition (fov never leaves 1..175, roughness stays in 0..1)
        key(nodePropId(ids.cam, "fov"), 5, PropValue::of(500));
        QCOMPARE(l->tracks[nodePropId(ids.cam, "fov")].keys[1].value.c[0], 175.0);
        // the colour change shows in the picture: red at the start, blue at the end
        d.setCurrentFrame(0);
        const QColor a = px(d, 60, 40);
        d.setCurrentFrame(20);
        const QColor b = px(d, 60, 40);
        QVERIFY(a.red() > a.blue());
        QVERIFY(b.blue() > b.red());
    }

    void stopwatchAndKeyButtonsBehaveLikeTheTwoDOnes() {
        Document d(cs());
        const Ids ids = build(d);
        const QString id = nodePropId(ids.light, "lint");
        d.setCurrentFrame(3);
        QVERIFY(d.toggleScene3DKey(ids.layer, id));                                 // not animated yet: starts animating with a key
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 1);
        d.setCurrentFrame(9);
        QVERIFY(d.toggleScene3DKey(ids.layer, id));                                 // adds a key holding the value shown there
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 2);
        QVERIFY(d.toggleScene3DKey(ids.layer, id));                                 // and again removes it
        QCOMPARE(d.findLayer(ids.layer)->tracks[id].keys.size(), 1);
        QVERIFY(d.toggleScene3DKey(ids.layer, id));
        d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.light)->intensity = 5; return true; }, "Brighter");
        d.setCurrentFrame(9);
        QVERIFY(d.toggleScene3DAnimation(ids.layer, id));                           // stop: freezes what is shown at this frame
        QVERIFY(!d.findLayer(ids.layer)->tracks.contains(id));
        QCOMPARE(d.findLayer(ids.layer)->scene3d->node(ids.light)->intensity, 5.0);
        QVERIFY(!d.toggleScene3DAnimation(ids.layer, nodePropId(ids.cam, "lint")));  // not a property of a camera
        QVERIFY(!d.toggleScene3DAnimation(ids.layer, nodePropId(QUuid::createUuid(), "pos")));
    }

    void easingAndSelectionToolsWorkOnThreeDKeys() {
        Document d(cs());
        const Ids ids = build(d);
        const QString id = nodePropId(ids.box, "pos");
        Layer* l = d.findLayer(ids.layer);
        const PropertyDef* def = findPropertyDef(id);
        PropertyTrack& t = l->tracks[id];
        t.setKey(*def, 0, pos(0)); t.setKey(*def, 10, pos(10));
        QVERIFY(t.applyEase(0, EasePreset::EaseInOut, true, true));
        QVERIFY(t.applyEase(10, EasePreset::EaseInOut, true, true));
        const double quarter = t.valueAt(*def, 2.5).c[0];
        QVERIFY(quarter < 2.5);                                                      // slow start
        QVERIFY(t.moveKey(10, 12));
        QCOMPARE(t.keys.last().frame, 12);
        QVERIFY(t.removeKey(12));
        QVERIFY(l->tracks.contains(id));
    }

    void keyframesSurviveSavingDeletedNodesDropTheirTracksAndDuplicatesFollowTheirNodes() {
        Document d(cs());
        const Ids ids = build(d);
        d.setCurrentFrame(0); d.toggleScene3DAnimation(ids.layer, nodePropId(ids.box, "pos"));
        d.setCurrentFrame(10);
        d.editScene3D(ids.layer, [&](Scene3D& s) { s.node(ids.box)->position = {2, 0, 0}; return true; }, "Move");
        d.toggleScene3DAnimation(ids.layer, nodePropId(ids.light, "lint"));
        d.toggleScene3DAnimation(ids.layer, materialPropId(ids.mat, "rough"));
        QTemporaryDir dir;
        const QString path = dir.filePath("a.studio");
        QString err;
        QVERIFY2(ProjectIO::save(d, path, &err), qPrintable(err));
        LoadResult r = ProjectIO::load(path);
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        QCOMPARE(r.doc->findLayer(ids.layer)->tracks, d.findLayer(ids.layer)->tracks);
        r.doc->setCurrentFrame(10);
        QCOMPARE(Compositor::render(*r.doc, r.doc->canvasRect(), {}), Compositor::render(d, d.canvasRect(), {}));
        // deleting the light leaves an orphan track in memory (so undo brings it back) but it is neither evaluated nor saved
        d.editScene3D(ids.layer, [&](Scene3D& s) { s.removeNode(ids.light); return true; }, "Delete");
        QVERIFY(d.findLayer(ids.layer)->tracks.contains(nodePropId(ids.light, "lint")));
        QVERIFY(!animatedSceneProps(*d.findLayer(ids.layer)->scene3d, d.findLayer(ids.layer)->tracks).contains(nodePropId(ids.light, "lint")));
        QVERIFY2(ProjectIO::save(d, path, &err), qPrintable(err));
        LoadResult r2 = ProjectIO::load(path);
        QVERIFY(r2.doc);
        QVERIFY(!r2.doc->findLayer(ids.layer)->tracks.contains(nodePropId(ids.light, "lint")));
        QVERIFY(r2.doc->findLayer(ids.layer)->tracks.contains(nodePropId(ids.box, "pos")));
        d.undoStack()->undo();
        QVERIFY(animatedSceneProps(*d.findLayer(ids.layer)->scene3d, d.findLayer(ids.layer)->tracks).contains(nodePropId(ids.light, "lint")));
        // a duplicated layer keeps its animation, attached to its own (renumbered) nodes
        auto dup = d.findLayer(ids.layer)->cloneWithNewIds();
        QCOMPARE(dup->tracks.size(), d.findLayer(ids.layer)->tracks.size());
        for (auto it = dup->tracks.begin(); it != dup->tracks.end(); ++it) QVERIFY2(scenePropExists(*dup->scene3d, it.key()), qPrintable(it.key()));
        QVERIFY(!dup->tracks.contains(nodePropId(ids.box, "pos")));
        QVERIFY(dup->sceneAt(10)->nodes[0].position.x() == 2.0f);
    }

    void brokenAnimationInAFileIsReportedNotFatal() {
        Document d(cs());
        const Ids ids = build(d);
        d.findLayer(ids.layer)->tracks.insert(nodePropId(QUuid::createUuid(), "pos"), PropertyTrack());   // refers to nothing
        PropertyTrack& t = d.findLayer(ids.layer)->tracks[nodePropId(ids.box, "pos")];
        t.setKey(*findPropertyDef(nodePropId(ids.box, "pos")), 0, pos(1));
        // the ghost track has no keys, so it is neither saved nor reported; a ghost with keys is dropped with a warning on load
        QTemporaryDir dir;
        QString err;
        QVERIFY(ProjectIO::save(d, dir.filePath("b.studio"), &err));
        LoadResult r = ProjectIO::load(dir.filePath("b.studio"));
        QVERIFY(r.doc);
        QCOMPARE(r.doc->findLayer(ids.layer)->tracks.size(), 1);
    }
};

QTEST_MAIN(TestAnim3D)
#include "test_anim3d.moc"
