#include <QtTest>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"
#include "Scene3D/Render3D.h"
#include "Scene3D/Scene3D.h"

using namespace mynd;

class TestScene3D : public QObject {
    Q_OBJECT
    static QUuid addMesh(Scene3D& s, std::shared_ptr<Mesh> m, QColor c, QVector3D pos = {}) {
        Material mat; mat.id = QUuid::createUuid(); mat.baseColor = c; mat.roughness = 1.0;
        s.materials.push_back(mat);
        m->parts = {{0, int(m->indices.size()), mat.id}};
        s.meshes[m->id] = m;
        Node3D n; n.kind = NodeKind::Mesh; n.name = m->name; n.mesh = m->id; n.position = pos;
        return s.addNode(n);
    }
    static QUuid addCamera(Scene3D& s, QVector3D pos) {
        Node3D c; c.kind = NodeKind::Camera; c.name = "Camera"; c.position = pos;
        return s.addNode(c);
    }
    static QUuid addLight(Scene3D& s, LightType t, QVector3D pos, QVector3D rot = {}) {
        Node3D l; l.kind = NodeKind::Light; l.lightType = t; l.position = pos; l.rotation = rot; l.name = "Light";
        return s.addNode(l);
    }
private slots:
    void eulerRoundTripsThroughMatrices() {
        for (QVector3D e : {QVector3D(10, 20, 30), QVector3D(-45, 60, 170), QVector3D(0, 89, 0), QVector3D(30, 0, -80)}) {
            Node3D n; n.rotation = e; n.position = {1, 2, 3}; n.scale = {2, 3, 4};
            QVector3D t, r, sc;
            decomposeMatrix(n.localMatrix(), &t, &r, &sc);
            QVERIFY((t - n.position).length() < 1e-4);
            QVERIFY((sc - n.scale).length() < 1e-4);
            Node3D m; m.rotation = r; m.position = t; m.scale = sc;               // same matrix, maybe other angles
            const QMatrix4x4 a = n.localMatrix(), b = m.localMatrix();
            for (int i = 0; i < 16; ++i) QVERIFY(std::abs(a.constData()[i] - b.constData()[i]) < 1e-3);
        }
    }

    void aCubeRendersInFrontOfTheCamera() {
        Scene3D s;
        addMesh(s, makeCube(2), QColor(255, 0, 0));
        addCamera(s, {0, 0, 6});
        addLight(s, LightType::Directional, {0, 0, 0}, {0, 0, 0});          // shines down -Z onto the front face
        s.ambientIntensity = 0.2;
        const QImage img = renderScene(s, sceneCamera(s, 1.0), QSize(120, 120));
        const QColor c = img.pixelColor(60, 60);
        QVERIFY2(c.red() > 200 && c.green() < 30 && c.blue() < 30 && c.alpha() == 255, qPrintable(c.name()));
        QCOMPARE(img.pixelColor(2, 2).alpha(), 0);                           // empty corner stays transparent
        // a cube 2 units wide at distance 5 (front face) covers fov-dependent area: roughly centred
        QVERIFY(img.pixelColor(40, 60).alpha() == 255);
        QVERIFY(img.pixelColor(110, 60).alpha() == 0);
    }

    void nearerObjectsHideFartherOnesAndLightingFollowsTheNormal() {
        Scene3D s;
        addMesh(s, makeCube(2), QColor(0, 0, 255), {0, 0, -3});
        addMesh(s, makeCube(1), QColor(0, 255, 0), {0, 0, 1});
        addCamera(s, {0, 0, 6});
        s.ambientIntensity = 1.0;                                            // flat lit
        QImage img = renderScene(s, sceneCamera(s, 1.0), QSize(100, 100));
        QVERIFY(img.pixelColor(50, 50).green() > 200 && img.pixelColor(50, 50).blue() < 50);   // green in front
        QVERIFY(img.pixelColor(50, 50).alpha() == 255);
        // light from the side: a sphere is lit on that side only
        Scene3D t;
        addMesh(t, makeSphere(1.0), QColor(255, 255, 255));
        addCamera(t, {0, 0, 5});
        t.ambientIntensity = 0.0;
        addLight(t, LightType::Directional, {}, {0, -90, 0});               // travels along +X: lights the -X side (screen left)
        img = renderScene(t, sceneCamera(t, 1.0), QSize(100, 100));
        QVERIFY(img.pixelColor(35, 50).red() > img.pixelColor(65, 50).red() + 60);
    }

    void backfacesAreCulledUnlessDoubleSided() {
        Scene3D s;
        const QUuid n = addMesh(s, makePlane(2), QColor(255, 255, 255));
        addCamera(s, {0, 0, 4});
        s.node(n)->rotation = {90, 0, 0};                                    // plane normal now +Z, faces the camera
        s.ambientIntensity = 1;
        QVERIFY(renderScene(s, sceneCamera(s, 1.0), QSize(60, 60)).pixelColor(30, 30).alpha() == 255);
        s.node(n)->rotation = {-90, 0, 0};                                           // normal -Z: back toward the camera
        QCOMPARE(renderScene(s, sceneCamera(s, 1.0), QSize(60, 60)).pixelColor(30, 30).alpha(), 0);
        s.materials[0].doubleSided = true;
        QVERIFY(renderScene(s, sceneCamera(s, 1.0), QSize(60, 60)).pixelColor(30, 30).alpha() == 255);
    }

    void objectsBehindTheCameraAndPartiallyClippedStillRender() {
        Scene3D s;
        addMesh(s, makePlane(40), QColor(200, 200, 200), {0, -1, 0});        // a floor that passes under and behind the camera
        addCamera(s, {0, 0, 0});
        s.ambientIntensity = 1;
        const QImage img = renderScene(s, sceneCamera(s, 1.0), QSize(80, 80));
        QVERIFY(img.pixelColor(40, 70).alpha() == 255);                      // floor in the lower half
        QCOMPARE(img.pixelColor(40, 5).alpha(), 0);                          // sky
    }

    void texturesAndOpacityAreApplied() {
        Scene3D s;
        const QUuid n = addMesh(s, makePlane(2), QColor(255, 255, 255));
        s.node(n)->rotation = {90, 0, 0};
        QImage tex(2, 2, QImage::Format_ARGB32);
        tex.setPixelColor(0, 0, Qt::red); tex.setPixelColor(1, 0, Qt::green); tex.setPixelColor(0, 1, Qt::blue); tex.setPixelColor(1, 1, Qt::white);
        const QUuid tid = QUuid::createUuid();
        s.textures[tid] = tex;
        s.materials[0].baseTexture = tid;
        s.ambientIntensity = 1;
        addCamera(s, {0, 0, 2.5});
        const QImage img = renderScene(s, sceneCamera(s, 1.0), QSize(100, 100));
        // plane after rotation: u to the right, v up (texture row 0 = top). Top-left texel red, top-right green, bottom-left blue.
        QVERIFY(img.pixelColor(20, 20).red() > 200 && img.pixelColor(20, 20).green() < 60);
        QVERIFY(img.pixelColor(80, 20).green() > 200 && img.pixelColor(80, 20).red() < 60);
        QVERIFY(img.pixelColor(20, 80).blue() > 200 && img.pixelColor(20, 80).red() < 60);
        // half transparent material blends with the transparent background
        s.materials[0].baseTexture = QUuid();
        s.materials[0].baseColor = QColor(255, 0, 0, 128);
        const QColor c = renderScene(s, sceneCamera(s, 1.0), QSize(100, 100)).pixelColor(50, 50);
        QVERIFY(c.alpha() > 100 && c.alpha() < 160);
    }

    void pickingFindsTheNearestObjectAndMarkers() {
        Scene3D s;
        const QUuid far = addMesh(s, makeCube(2), Qt::blue, {0, 0, -3});
        const QUuid near = addMesh(s, makeCube(1), Qt::green, {0, 0, 1});
        const QUuid light = addLight(s, LightType::Point, {3, 0, 0});
        addCamera(s, {0, 0, 6});
        const RenderCamera cam = sceneCamera(s, 1.0);
        const QSize sz(200, 200);
        QCOMPARE(pickNode(s, cam, sz, QPointF(100, 100)).node, near);
        QPointF lp; QVERIFY(projectToScreen(cam, QVector3D(3, 0, 0), sz, &lp));
        QCOMPARE(pickNode(s, cam, sz, lp).node, light);
        QVERIFY(pickNode(s, cam, sz, QPointF(2, 2)).node.isNull());
        s.node(near)->visible = false;
        QCOMPARE(pickNode(s, cam, sz, QPointF(100, 100)).node, far);
    }

    void hierarchyReparentKeepsWorldTransformAndRefusesCycles() {
        Scene3D s;
        Node3D a; a.kind = NodeKind::Group; a.name = "A"; a.position = {5, 0, 0}; a.rotation = {0, 90, 0}; a.scale = {2, 2, 2};
        const QUuid ia = s.addNode(a);
        Node3D b; b.kind = NodeKind::Group; b.name = "B"; b.position = {0, 1, 0};
        const QUuid ib = s.addNode(b);
        const QVector3D before = s.worldMatrix(ib).map(QVector3D(1, 0, 0));
        QVERIFY(s.reparent(ib, ia));
        QCOMPARE(s.node(ib)->parent, ia);
        QVERIFY((s.worldMatrix(ib).map(QVector3D(1, 0, 0)) - before).length() < 1e-3);
        QVERIFY(!s.reparent(ia, ib));                                        // would make a cycle
        QVERIFY(!s.reparent(ia, ia));
        s.removeNode(ia);                                                    // takes the child with it
        QVERIFY(s.nodes.empty());
    }


    void aSceneLayerRendersInTheCompositionUndoesAndSurvivesSaving() {
        CompositionSettings cs; cs.width = 120; cs.height = 80; cs.durationFrames = 10; cs.background = Qt::white;
        Document d(cs);
        const QUuid lid = d.addScene3DLayer("Scene");
        Material mat; mat.id = QUuid::createUuid(); mat.baseColor = Qt::red; mat.roughness = 1;
        auto cube = makeCube(2); cube->parts = {{0, int(cube->indices.size()), mat.id}};
        d.undoStack()->clear();
        QVERIFY(d.editScene3D(lid, [&](Scene3D& s) {
            s.materials.push_back(mat); s.meshes[cube->id] = cube;
            Node3D n; n.kind = NodeKind::Mesh; n.mesh = cube->id; n.name = "Box"; s.addNode(n);
            Node3D c; c.kind = NodeKind::Camera; c.position = {0, 0, 6}; c.name = "Cam"; s.addNode(c);
            s.ambientIntensity = 1; return true; }, "Build"));
        auto px = [&](int x, int y) { return Compositor::render(d, d.canvasRect(), {}).pixelColor(x, y); };
        QCOMPARE(px(60, 40), QColor(Qt::red));
        QCOMPARE(px(2, 2), QColor(Qt::white));                               // 3D layer is transparent outside the objects
        d.undoStack()->undo();
        QCOMPARE(px(60, 40), QColor(Qt::white));
        d.undoStack()->redo();
        QCOMPARE(px(60, 40), QColor(Qt::red));
        // dragging merges into a single undo step
        const QUuid box = d.findLayer(lid)->scene3d->nodes[0].id;
        for (int i = 1; i <= 5; ++i)
            d.editScene3D(lid, [&](Scene3D& s) { s.node(box)->position = QVector3D(float(i), 0, 0); return true; }, "Move", "drag");
        QCOMPARE(d.undoStack()->count(), 2);
        // save, reopen, same picture and same scene
        QTemporaryDir dir;
        const QString path = dir.filePath("scene.studio");
        QString err;
        QVERIFY2(ProjectIO::save(d, path, &err), qPrintable(err));
        LoadResult r = ProjectIO::load(path);
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join("; ")));
        const Layer* l = r.doc->findLayer(lid);
        QVERIFY(l && l->scene3d);
        QCOMPARE(l->scene3d->nodes.size(), size_t(2));
        QCOMPARE(l->scene3d->node(box)->position, QVector3D(5, 0, 0));
        QCOMPARE(l->scene3d->meshes.size(), size_t(1));
        QCOMPARE(Compositor::render(*r.doc, r.doc->canvasRect(), {}), Compositor::render(d, d.canvasRect(), {}));
        // a duplicated layer is independent
        auto dup = d.findLayer(lid)->cloneWithNewIds();
        dup->scene3d->nodes[0].position = {9, 9, 9};
        QCOMPARE(d.findLayer(lid)->scene3d->node(box)->position, QVector3D(5, 0, 0));
        QVERIFY(dup->scene3d->nodes[0].id != box);
    }

    void sceneSerialisesAndRejectsDamagedData() {
        Scene3D s;
        const QUuid m = addMesh(s, makeCube(1), Qt::red);
        addCamera(s, {0, 0, 5});
        addLight(s, LightType::Spot, {1, 2, 3});
        s.node(m)->rotation = {10, 20, 30};
        const QJsonObject j = s.toJson();
        Scene3D r; QString err;
        QVERIFY2(Scene3D::fromJson(j, r, &err), qPrintable(err));
        QCOMPARE(r.nodes.size(), s.nodes.size());
        QVERIFY(r.nodes[0] == s.nodes[0]);
        QVERIFY(r.materials[0] == s.materials[0]);
        QCOMPARE(r.activeCamera, s.activeCamera);
        // mesh blobs round-trip
        const Mesh& mesh = *s.meshes.begin()->second;
        QString e2;
        auto back = Mesh::fromJson(mesh.toJson(), mesh.geometryBlob(), &e2);
        QVERIFY2(back, qPrintable(e2));
        QCOMPARE(back->positions.size(), mesh.positions.size());
        QCOMPARE(back->indices, mesh.indices);
        QVERIFY(!Mesh::fromJson(mesh.toJson(), mesh.geometryBlob().left(20), &e2));     // truncated geometry
        QJsonObject bad = j; QJsonArray na = bad["nodes"].toArray(); QJsonObject n0 = na[0].toObject(); n0["kind"] = "banana"; na[0] = n0; bad["nodes"] = na;
        QVERIFY(!Scene3D::fromJson(bad, r, &err));
        // validation reports dangling references instead of crashing
        s.nodes[0].parent = QUuid::createUuid();
        QVERIFY(!s.validate().isEmpty());
    }
};

QTEST_MAIN(TestScene3D)
#include "test_scene3d.moc"
