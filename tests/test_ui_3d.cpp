#include <QtTest>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Core/Settings.h"
#include "UI/MainWindow.h"
#include "UI/Workspaces.h"

using namespace mynd;

class TestUi3D : public QObject {
    Q_OBJECT
    std::unique_ptr<MainWindow> w;
    EditorSession* s = nullptr;
    Document* doc = nullptr;
    Scene3DActions* a = nullptr;
    Viewport3D* vp = nullptr;

    QColor px(int x, int y) { return Compositor::render(*doc, doc->canvasRect(), {}).pixelColor(x, y); }
    Scene3D* scene() { Layer* l = s->active3DLayer(); return l ? l->scene3d.get() : nullptr; }
    QPointF handle(Viewport3D::Handle h) { QPointF p; return vp->handlePosition(h, &p) ? p : QPointF(-1, -1); }
    void dragCanvas(QPointF from, QPointF to, Qt::KeyboardModifiers m = {}, Qt::MouseButton b = Qt::LeftButton) {
        QWidget* c = vp->canvas();
        QTest::mousePress(c, b, m, from.toPoint());
        QTest::mouseMove(c, ((from + to) / 2).toPoint());
        QTest::mouseMove(c, to.toPoint());
        QTest::mouseRelease(c, b, m, to.toPoint());
    }
    // A scene with a cube, a light and a camera that frames it.
    QUuid buildScene() {
        const QUuid cube = a->createPrimitive(Primitive::Cube);
        a->createLight(LightType::Directional);
        a->createCamera();
        s->setSelectedNode(cube);
        return cube;
    }

private slots:
    void init() {
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        w = std::make_unique<MainWindow>();
        CompositionSettings cs; cs.width = 400; cs.height = 300; cs.durationFrames = 24; cs.background = Qt::white;
        w->newDocument(cs, false);
        w->resize(1500, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->workspaces()->apply("3D");
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        QApplication::processEvents();
        s = w->session(); doc = w->document(); a = w->scene3d(); vp = w->viewport3d();
        doc->undoStack()->clear();
    }
    void cleanup() { doc->undoStack()->clear(); doc->markSaved(); w->close(); w.reset(); }

    void the3DWorkspaceShowsThePanelsAndOtherWorkspacesHideThem() {
        auto vis = [&](const char* k) { return w->findChild<QDockWidget*>(QString("dock.") + k)->isVisible(); };
        QVERIFY(vis("viewport3d") && vis("scene3d"));
        w->workspaces()->apply("Drawing");
        QVERIFY(!vis("viewport3d") && !vis("scene3d"));
        // creating a 3D layer brings the panels back even from a workspace that does not have them
        a->createPrimitive(Primitive::Cube);
        QApplication::processEvents();
        QVERIFY(vis("viewport3d") && vis("scene3d"));
    }

    void buildingASceneShowsItInTheCompositionAndEveryStepIsUndoable() {
        const QUuid cube = buildScene();
        QVERIFY(s->active3DLayer());
        QCOMPARE(scene()->nodes.size(), size_t(3));
        QCOMPARE(w->scene3dPanel()->tree()->topLevelItemCount(), 3);
        QVERIFY(px(200, 150) != QColor(Qt::white));                         // the cube is in the picture
        QCOMPARE(px(2, 2), QColor(Qt::white));
        // each add was one undo step; undoing everything leaves nothing behind
        int guard = 0;
        while (doc->undoStack()->canUndo() && guard++ < 20) doc->undoStack()->undo();
        QVERIFY(!doc->undoStack()->canUndo());
        QVERIFY(doc->root()->children.empty());
        while (doc->undoStack()->canRedo()) doc->undoStack()->redo();
        QVERIFY(s->active3DLayer() || doc->root()->children.size() == 1);
        Layer* l = nullptr; Document::forEachLayer(doc->root(), [&](Layer* x) { if (x->type == LayerType::Scene3D) l = x; });
        QVERIFY(l && l->scene3d->node(cube));
    }

    void theInspectorEditsAreUndoableAndMergeWhileTyping() {
        const QUuid cube = buildScene();
        Scene3DPanel* p = w->scene3dPanel();
        QVERIFY(p->transformGroup()->isVisibleTo(p) && p->materialGroup()->isVisibleTo(p));
        QVERIFY(!p->lightGroup()->isVisibleTo(p));
        const int before = doc->undoStack()->count();
        p->posField(0)->setValue(1.0); p->posField(0)->setValue(1.5); p->posField(0)->setValue(2.0);
        QCOMPARE(scene()->node(cube)->position.x(), 2.0f);
        QCOMPARE(doc->undoStack()->count(), before + 1);                    // one step for the whole edit
        doc->undoStack()->undo();
        QCOMPARE(scene()->node(cube)->position.x(), 0.0f);
        // material edits
        p->roughnessField()->setValue(0.2);
        const Mesh& m = *scene()->meshes.at(scene()->node(cube)->mesh);
        QCOMPARE(scene()->material(m.parts[0].material)->roughness, 0.2);
        // selecting the light swaps the groups
        for (const auto& n : scene()->nodes) if (n.kind == NodeKind::Light) s->setSelectedNode(n.id);
        QVERIFY(p->lightGroup()->isVisibleTo(p) && !p->materialGroup()->isVisibleTo(p));
        p->intensityField()->setValue(2.5);
        for (const auto& n : scene()->nodes) if (n.kind == NodeKind::Light) QCOMPARE(n.intensity, 2.5);
    }

    void clickingPicksAndTheMoveGizmoDragsAlongAnAxis() {
        const QUuid cube = buildScene();
        s->setSelectedNode({});
        QApplication::processEvents();
        // click the cube: it gets selected
        QPointF c = handle(Viewport3D::Handle::Center);
        QVERIFY(c.x() < 0);                                                   // nothing selected: no handles
        QSize sz; const RenderCamera cam = vp->currentCamera(&sz);
        QPointF cs; QVERIFY(projectToScreen(cam, QVector3D(0, 0.5f, 0), sz, &cs));
        QTest::mouseClick(vp->canvas(), Qt::LeftButton, {}, cs.toPoint());
        QCOMPARE(s->selectedNode(), cube);
        // clicking empty space deselects
        QTest::mouseClick(vp->canvas(), Qt::LeftButton, {}, QPoint(3, vp->canvas()->height() - 3));
        QVERIFY(s->selectedNode().isNull());
        s->setSelectedNode(cube);
        // drag the X handle
        vp->setMode(Viewport3D::Mode::Move);
        const QPointF o = handle(Viewport3D::Handle::Center), hx = handle(Viewport3D::Handle::X);
        QVERIFY(o.x() >= 0 && hx.x() >= 0);
        QPointF dir = hx - o; dir /= std::hypot(dir.x(), dir.y());
        const int steps = doc->undoStack()->count();
        const float x0 = scene()->node(cube)->position.x();
        dragCanvas(hx, hx + dir * 40);
        QVERIFY2(scene()->node(cube)->position.x() > x0 + 0.05f, qPrintable(QString::number(scene()->node(cube)->position.x())));
        QVERIFY(std::abs(scene()->node(cube)->position.y() - 0.5f) < 1e-3 && std::abs(scene()->node(cube)->position.z()) < 1e-3);   // only X moved
        QCOMPARE(doc->undoStack()->count(), steps + 1);                      // the whole drag is one undo step
        doc->undoStack()->undo();
        QCOMPARE(scene()->node(cube)->position.x(), x0);
        // a second drag is a separate step
        dragCanvas(hx, hx + dir * 20); dragCanvas(handle(Viewport3D::Handle::X), handle(Viewport3D::Handle::X) + dir * 20);
        QCOMPARE(doc->undoStack()->count(), steps + 2);
    }

    void rotateAndScaleGizmosWork() {
        const QUuid cube = buildScene();
        vp->setMode(Viewport3D::Mode::Rotate);
        QPointF o = handle(Viewport3D::Handle::Center);
        QPointF r = handle(Viewport3D::Handle::Y);                           // a point on the Y ring
        QVERIFY(r.x() >= 0);
        dragCanvas(r, r + QPointF(0, 0) + QPointF(40, 15));
        const QVector3D rot = scene()->node(cube)->rotation;
        QVERIFY2(rot.length() > 1.0f, qPrintable(QString("%1 %2 %3").arg(rot.x()).arg(rot.y()).arg(rot.z())));
        QVERIFY(std::abs(scene()->node(cube)->scale.x() - 1) < 1e-4);
        doc->undoStack()->undo();
        QCOMPARE(scene()->node(cube)->rotation, QVector3D(0, 0, 0));
        // scale along X
        vp->setMode(Viewport3D::Mode::Scale);
        o = handle(Viewport3D::Handle::Center);
        const QPointF hx = handle(Viewport3D::Handle::X);
        QPointF dir = hx - o; dir /= std::hypot(dir.x(), dir.y());
        dragCanvas(hx, hx + dir * 40);
        QVERIFY(scene()->node(cube)->scale.x() > 1.1f);
        QVERIFY(std::abs(scene()->node(cube)->scale.y() - 1) < 1e-4);
        // uniform scale from the centre handle
        const float x1 = scene()->node(cube)->scale.x();
        dragCanvas(o, o + QPointF(60, 0));
        QVERIFY(scene()->node(cube)->scale.y() > 1.2f && scene()->node(cube)->scale.x() > x1);
    }

    void cameraViewOrbitAndFraming() {
        buildScene();
        OrbitCamera before = vp->orbit();
        dragCanvas(QPointF(200, 150), QPointF(260, 180), Qt::AltModifier);   // Alt + drag orbits
        QVERIFY(vp->orbit().yawDeg != before.yawDeg);
        const double d0 = vp->orbit().distance;
        QWheelEvent we(QPointF(100, 100), vp->canvas()->mapToGlobal(QPointF(100, 100)), {}, QPoint(0, 120), Qt::NoButton, {}, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vp->canvas(), &we);
        QVERIFY(vp->orbit().distance < d0);                                   // wheel zooms in
        vp->setCameraView(true);
        QSize sz; vp->currentCamera(&sz);
        QVERIFY(std::abs(double(sz.width()) / sz.height() - 400.0 / 300.0) < 0.02);   // letterboxed to the composition
        vp->setCameraView(false);
        vp->frame(false);
        QVERIFY(vp->orbit().distance > 0.5 && vp->orbit().distance < 10);
    }

    void hierarchyDragAndDropReparentsAndDeleteDuplicateWork() {
        const QUuid cube = buildScene();
        const QUuid grp = a->createGroup();
        w->scene3dPanel()->dropOn(cube, grp);
        QCOMPARE(scene()->node(cube)->parent, grp);
        QCOMPARE(w->scene3dPanel()->tree()->topLevelItemCount(), 3);          // group, light, camera (cube moved under the group)
        w->scene3dPanel()->dropOn(grp, cube);                                  // would be a cycle: refused
        QVERIFY(scene()->node(grp)->parent.isNull());
        s->setSelectedNode(grp);
        const QUuid dup = a->duplicateSelected();
        QVERIFY(!dup.isNull() && dup != grp);
        QCOMPARE(scene()->childrenOf(dup).size(), 1);                          // children came along
        QVERIFY(scene()->childrenOf(dup)[0] != cube);
        QVERIFY(a->deleteSelected());
        QVERIFY(!scene()->node(dup));
        QVERIFY(s->selectedNode().isNull());
        doc->undoStack()->undo();
        QVERIFY(scene()->node(dup));
    }

    void importingAModelAddsALightAndCameraAndSurvivesSaving() {
        QTemporaryDir dir;
        QFile mtl(dir.filePath("m.mtl")); QVERIFY(mtl.open(QIODevice::WriteOnly)); mtl.write("newmtl Red\nKd 1 0 0\n"); mtl.close();
        QFile obj(dir.filePath("quad.obj")); QVERIFY(obj.open(QIODevice::WriteOnly));
        obj.write("mtllib m.mtl\no Quad\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nusemtl Red\nf 1//1 2//1 3//1 4//1\n"); obj.close();
        QString err; QStringList warn;
        const QUuid root = a->importModelFile(obj.fileName(), &err, &warn);
        QVERIFY2(!root.isNull(), qPrintable(err));
        QVERIFY(s->active3DLayer());
        QVERIFY(!scene()->firstCamera().isNull());
        bool light = false; for (const auto& n : scene()->nodes) light |= n.kind == NodeKind::Light;
        QVERIFY(light);
        QVERIFY(px(200, 150).red() > 150 && px(200, 150).green() < 120);       // the red quad is visible in the composition
        // bad files report an error and change nothing
        const int nodes = int(scene()->nodes.size());
        QVERIFY(a->importModelFile(dir.filePath("missing.glb"), &err).isNull());
        QVERIFY(!err.isEmpty());
        QCOMPARE(int(scene()->nodes.size()), nodes);
        // save + reopen keeps the scene and the picture
        const QString path = dir.filePath("proj.studio");
        QVERIFY(ProjectIO::save(*doc, path, &err));
        LoadResult lr = ProjectIO::load(path);
        QVERIFY2(lr.doc, qPrintable(lr.error));
        QVERIFY2(lr.warnings.isEmpty(), qPrintable(lr.warnings.join(";")));
        QCOMPARE(Compositor::render(*lr.doc, lr.doc->canvasRect(), {}), Compositor::render(*doc, doc->canvasRect(), {}));
        // the whole import is one undo step
        const int before = doc->undoStack()->index();
        doc->undoStack()->undo();
        QVERIFY(doc->undoStack()->index() < before);
        QVERIFY(!s->active3DLayer());                                          // the layer the import created went with it
        QCOMPARE(px(200, 150), QColor(Qt::white));
        doc->undoStack()->redo();
        QVERIFY(s->active3DLayer() || px(200, 150).red() > 150);
    }

    void texturesCanBeAssignedRenderAndSurviveSaving() {
        const QUuid cube = buildScene();
        Scene3DPanel* p = w->scene3dPanel();
        QImage tex(2, 2, QImage::Format_ARGB32); tex.fill(QColor(0, 200, 0));
        QVERIFY(p->setBaseTexture(tex));
        const Mesh& m = *scene()->meshes.at(scene()->node(cube)->mesh);
        const Material* mat = scene()->material(m.parts[0].material);
        QVERIFY(mat && !mat->baseTexture.isNull() && scene()->textures.count(mat->baseTexture));
        const QColor c = px(200, 150);
        QVERIFY2(c.green() > c.red() + 40 && c.green() > c.blue() + 40, qPrintable(c.name()));       // the cube turned green
        QTemporaryDir dir; QString err;
        QVERIFY(ProjectIO::save(*doc, dir.filePath("t.studio"), &err));
        LoadResult lr = ProjectIO::load(dir.filePath("t.studio"));
        QVERIFY2(lr.doc && lr.warnings.isEmpty(), qPrintable(lr.error + lr.warnings.join(";")));
        QCOMPARE(Compositor::render(*lr.doc, lr.doc->canvasRect(), {}), Compositor::render(*doc, doc->canvasRect(), {}));
        doc->undoStack()->undo();
        QVERIFY(scene()->textures.empty());                                                              // undo removes the texture again
        QVERIFY(px(200, 150).green() < px(200, 150).red() + 40);
    }

    void zzScreenshotForReview() {
        const QByteArray out = qgetenv("MYND_SHOT");
        if (out.isEmpty()) QSKIP("set MYND_SHOT to write a screenshot");
        QTemporaryDir dir;
        QFile mtl(dir.filePath("m.mtl")); mtl.open(QIODevice::WriteOnly); mtl.write("newmtl Red\nKd 0.9 0.2 0.2\nNs 80\nnewmtl Blue\nKd 0.2 0.4 0.9\n"); mtl.close();
        QFile obj(dir.filePath("m.obj")); obj.open(QIODevice::WriteOnly);
        QByteArray t = "mtllib m.mtl\no Pyramid\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 1.6 0\nusemtl Red\nf 1 2 5\nf 2 3 5\nf 3 4 5\nf 4 1 5\nusemtl Blue\nf 4 3 2 1\n";
        obj.write(t); obj.close();
        QString err;
        a->importModelFile(obj.fileName(), &err);
        a->createPrimitive(Primitive::Sphere);
        s->setSelectedNode(scene()->nodes.back().id);
        scene()->nodes.back();
        doc->editScene3D(s->active3DLayer()->id, [&](Scene3D& sc) { sc.nodes.back().position = QVector3D(1.8f, 0.5f, 0.5f); return true; }, "move");
        a->createPrimitive(Primitive::Plane);
        w->resize(1500, 900);
        QApplication::processEvents();
        w->grab().save(QString::fromUtf8(out));
        vp->setCameraView(true);
        QApplication::processEvents();
        w->grab().save(QString::fromUtf8(out) + ".cam.png");
    }

    void lockedLayersRefuseEdits() {
        const QUuid cube = buildScene();
        Layer* l = s->active3DLayer();
        l->props.locked = true;
        w->scene3dPanel()->posField(0)->setValue(5.0);
        QCOMPARE(scene()->node(cube)->position.x(), 0.0f);
        QVERIFY(!a->deleteSelected());
        l->props.locked = false;
    }
};

QTEST_MAIN(TestUi3D)
#include "test_ui_3d.moc"
