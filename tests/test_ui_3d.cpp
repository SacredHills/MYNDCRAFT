#include <QtTest>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>
#include <QListWidget>

#include "TestEnv.h"
#include "AnimGltf.h"
#include "Core/ProjectIO.h"
#include "Core/Settings.h"
#include "UI/GraphEditor.h"
#include "UI/MainWindow.h"
#include "UI/Panels.h"
#include "UI/ScrubSpinBox.h"
#include "UI/TimelinePanel.h"
#include "Scene3D/Anim3D.h"
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

    void theInspectorStopwatchAndAutoKeyAnimateTheSelectedObjectAndShowUpInTheGraphEditor() {
        const QUuid cube = buildScene();
        Scene3DPanel* p = w->scene3dPanel();
        const QString id = nodePropId(cube, "pos");
        doc->setCurrentFrame(0);
        QVERIFY(p->stopwatch("pos") && !p->stopwatch("pos")->isChecked());
        p->stopwatch("pos")->click();                                                // key at frame 0
        QVERIFY(p->stopwatch("pos")->isChecked());
        QVERIFY(s->active3DLayer()->tracks.contains(id));
        // typing a position at frame 12 keys it instead of moving the object everywhere
        doc->setCurrentFrame(12);
        p->posField(0)->setValue(3.0);
        QCOMPARE(s->active3DLayer()->tracks[id].keys.size(), 2);
        QCOMPARE(scene()->node(cube)->position.x(), 0.0f);
        doc->setCurrentFrame(6);
        QVERIFY(std::abs(p->posField(0)->value() - 1.5) < 1e-6);                    // the inspector shows the value at this frame
        // dragging the gizmo at a key-less frame of an animated property adds a key (auto-key does it for other properties)
        vp->setMode(Viewport3D::Mode::Move);
        const QPointF o = handle(Viewport3D::Handle::Center), hx = handle(Viewport3D::Handle::X);
        QPointF dir = hx - o; dir /= std::hypot(dir.x(), dir.y());
        dragCanvas(hx, hx + dir * 30);
        QCOMPARE(s->active3DLayer()->tracks[id].keys.size(), 3);
        QVERIFY(s->active3DLayer()->tracks[id].indexOfKey(6) >= 0);
        QCOMPARE(scene()->node(cube)->position.x(), 0.0f);                          // static value never changed
        // rotating (not animated, auto-key off) edits the static rotation; with auto-key on it keys it
        vp->setMode(Viewport3D::Mode::Rotate);
        QPointF r = handle(Viewport3D::Handle::Y);
        dragCanvas(r, r + QPointF(40, 15));
        QVERIFY(!s->active3DLayer()->tracks.contains(nodePropId(cube, "rot")));
        QVERIFY(scene()->node(cube)->rotation != QVector3D());
        s->setAutoKey(true);
        r = handle(Viewport3D::Handle::Y);
        dragCanvas(r, r + QPointF(40, 15));
        QVERIFY(s->active3DLayer()->tracks.contains(nodePropId(cube, "rot")));
        s->setAutoKey(false);
        // the graph editor lists the animated 3D properties by name
        auto* g = w->findChild<GraphEditorPanel*>();
        QVERIFY(g);
        QApplication::processEvents();
        bool found = false;
        for (int i = 0; i < g->propertyBox()->count(); ++i) if (g->propertyBox()->itemData(i).toString() == id) { found = true; QVERIFY(g->propertyBox()->itemText(i).contains("Position")); }
        QVERIFY(found);
        // and the timeline shows a row for it once the layer is expanded
        auto* tl = w->findChild<TimelineView*>();
        QVERIFY(tl);
        tl->setCellWidth(20);
        QCOMPARE(tl->rowOfProperty(s->activeLayerId(), id), -1);
        QTest::mouseClick(tl->viewport(), Qt::LeftButton, {}, tl->twirlPoint(tl->rowOfLayer(s->activeLayerId())));
        QVERIFY(tl->isExpanded(s->activeLayerId()));
        QVERIFY(tl->rowOfProperty(s->activeLayerId(), id) >= 0);
        QVERIFY(tl->rowOfProperty(s->activeLayerId(), nodePropId(cube, "rot")) >= 0);
        // everything undoes
        while (doc->undoStack()->canUndo()) doc->undoStack()->undo();
        QVERIFY(doc->root()->children.empty());
    }

    void anImportedAnimationIsListedPlayedAndEditableAsAPlacement() {
        QTemporaryDir dir;
        const QString path = dir.filePath("spin.gltf");
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(testmodels::animatedGltf()); f.close();
        QString err; QStringList warn;
        QVERIFY2(!a->importModelFile(path, &err, &warn).isNull(), qPrintable(err));
        QVERIFY2(warn.join("|").contains("animation clip"), qPrintable(warn.join("|")));
        Scene3DPanel* p = w->scene3dPanel();
        QApplication::processEvents();
        QVERIFY(p->clipGroup()->isVisibleTo(p));
        QCOMPARE(p->clipList()->count(), 1);
        QCOMPARE(p->placementList()->count(), 1);
        QUuid child;
        for (const Node3D& n : scene()->nodes) if (n.name == "Child") child = n.id;
        QVERIFY(!child.isNull());
        doc->setCurrentFrame(12);
        QCOMPARE(s->active3DLayer()->sceneAt(12)->node(child)->position, QVector3D(1, 0, 0));
        // retiming the placement through the panel moves the whole animation
        const int steps = doc->undoStack()->count();
        p->placementList()->setCurrentRow(0);
        s->active3DLayer();                                                      // (selection of the placement is panel state)
        QVERIFY(p->addSelectedClipToTimeline());
        QCOMPARE(p->placementList()->count(), 2);
        QCOMPARE(doc->undoStack()->count(), steps + 1);
        doc->undoStack()->undo();
        QCOMPARE(p->placementList()->count(), 1);
    }

    void aTwoDLayerBecomesACardInThreeDSpaceAndBackAgain() {
        const QUuid art = doc->addLayer(LayerType::Vector, "Art");
        VectorShape r = VectorShape::makeRect(QRectF(100, 75, 200, 150)); r.fill.color = Qt::red; r.stroke.enabled = false;
        doc->setShapes(art, {r}, "rect");
        s->setActiveLayer(art);
        doc->undoStack()->clear();
        QVERIFY(a->make3D->isEnabled() && !a->make3D->isChecked());
        const QColor before = px(200, 150);
        QCOMPARE(before, QColor(Qt::red));
        QString err;
        const QUuid card = a->makeLayer3D(art, &err);
        QVERIFY2(!card.isNull(), qPrintable(err));
        QApplication::processEvents();
        // a scene layer was made right above the art layer, with the card and a camera that frames it exactly
        QCOMPARE(doc->root()->children.size(), size_t(2));
        QCOMPARE(doc->root()->children[1]->type, LayerType::Scene3D);
        QVERIFY(scene() && scene()->node(card) && scene()->node(card)->cardLayer == art);
        QVERIFY(!scene()->firstCamera().isNull());
        QVERIFY(px(200, 150).red() > 200 && px(200, 150).green() < 60);                // same picture as before
        QCOMPARE(px(20, 20), QColor(Qt::white));
        QVERIFY(a->isLayer3D(art));
        QCOMPARE(w->scene3dPanel()->cardGroup()->isVisibleTo(w->scene3dPanel()), true);
        QVERIFY(!w->scene3dPanel()->materialGroup()->isVisibleTo(w->scene3dPanel()));
        // it is a real 3D object now: turning it changes the picture
        s->setSelectedNode(card);
        doc->editScene3D(s->activeLayerId(), [&](Scene3D& sc) { sc.node(card)->rotation = {0, 80, 0}; return true; }, "Turn");
        QVERIFY(px(300, 150) != QColor(Qt::red));
        // the checkable action reflects it from the source layer, and unchecking restores the plain layer
        s->setActiveLayer(art);
        QApplication::processEvents();
        QVERIFY(a->make3D->isChecked());
        a->make3D->trigger();
        QVERIFY(!a->isLayer3D(art));
        QApplication::processEvents();
        QCOMPARE(px(200, 150), QColor(Qt::red));
        QVERIFY(!a->make3D->isChecked());
        // the whole "make 3D" gesture is one undo step
        a->make3D->trigger();
        QVERIFY(a->isLayer3D(art));
        doc->undoStack()->undo();                                                       // removes the card
        QVERIFY(!a->isLayer3D(art));
        doc->undoStack()->undo();                                                       // undoes "Make 3D" as a whole
        QVERIFY(a->isLayer3D(art));
        while (doc->undoStack()->canUndo()) doc->undoStack()->undo();
        QCOMPARE(doc->root()->children.size(), size_t(1));
        QCOMPARE(px(200, 150), QColor(Qt::red));
        // a 3D scene layer cannot itself be made a card
        const QUuid sc2 = doc->addScene3DLayer();
        s->setActiveLayer(sc2);
        QVERIFY(a->makeLayer3D(sc2, &err).isNull() && !err.isEmpty());
    }

    void makingALayer2DAgainFromTheCardInspectorDoesNotUseFreedMemory() {
        const QUuid art = doc->addLayer(LayerType::Vector, "Art");
        VectorShape r = VectorShape::makeRect(QRectF(100, 75, 200, 150)); r.fill.color = Qt::red; r.stroke.enabled = false;
        doc->setShapes(art, {r}, "rect");
        const QUuid card = a->makeLayer3D(art);
        QVERIFY(!card.isNull());
        s->setSelectedNode(card);
        QApplication::processEvents();
        QPushButton* back = nullptr;
        for (QPushButton* b : w->scene3dPanel()->cardGroup()->findChildren<QPushButton*>()) back = b;
        QVERIFY(back);
        back->click();                                          // the node (and the id it holds) is deleted inside this click
        QApplication::processEvents();
        QVERIFY(!a->isLayer3D(art));
        QCOMPARE(px(200, 150), QColor(Qt::red));
        // unchecking from the menu action with the source layer active does the same
        a->makeLayer3D(art);
        s->setActiveLayer(art);
        QApplication::processEvents();
        QVERIFY(a->make3D->isChecked());
        a->make3D->trigger();
        QApplication::processEvents();
        QVERIFY(!a->isLayer3D(art));
        QCOMPARE(px(200, 150), QColor(Qt::red));
    }

    void layersCanBeCopiedCutAndPastedOfAnyType() {
        const QUuid scene3 = doc->addScene3DLayer("Scene");
        const QUuid vec = doc->addLayer(LayerType::Vector, "Vec");
        const QUuid grp = doc->addLayer(LayerType::Group, "Grp");
        doc->addLayer(LayerType::Raster, "Ras", grp);
        const int before = int(doc->root()->children.size());
        for (const QUuid& id : {scene3, vec, grp}) {
            s->setActiveLayer(id);
            QVERIFY(s->copyLayer(id));
            const QUuid pasted = s->pasteLayer();
            QVERIFY(!pasted.isNull() && pasted != id);
            QCOMPARE(doc->findLayer(pasted)->type, doc->findLayer(id)->type);
            QCOMPARE(doc->indexInParent(doc->findLayer(pasted)), doc->indexInParent(doc->findLayer(id)) + 1);   // right above the original
        }
        QCOMPARE(int(doc->root()->children.size()), before + 3);
        // pasting twice gives two independent layers; a pasted group keeps its children (with new ids)
        s->setActiveLayer(grp);
        s->copyLayer(grp);
        const QUuid g1 = s->pasteLayer(), g2 = s->pasteLayer();
        QVERIFY(g1 != g2);
        QCOMPARE(doc->findLayer(g1)->children.size(), size_t(1));
        QVERIFY(doc->findLayer(g1)->children[0]->id != doc->findLayer(grp)->children[0]->id);
        // cut removes the original and the paste restores it; everything is undoable
        const int n = int(doc->root()->children.size());
        QVERIFY(s->cutLayer(vec));
        QCOMPARE(int(doc->root()->children.size()), n - 1);
        QVERIFY(!s->pasteLayer().isNull());
        QCOMPARE(int(doc->root()->children.size()), n);
        doc->undoStack()->undo();
        QCOMPARE(int(doc->root()->children.size()), n - 1);
        // the clipboard is per document
        s->setDocument(nullptr);
        QVERIFY(!s->canPasteLayer());
    }

    void layerPanelHas3DColumnThatTogglesCardMode() {
        const QUuid vec = doc->addLayer(LayerType::Vector, "Vec");
        auto* lp = w->findChild<LayerPanel*>();
        QVERIFY(lp);
        auto* tree = lp->tree();
        QCOMPARE(tree->columnCount(), 3);
        auto find = [&](const QUuid& id) -> QTreeWidgetItem* {
            QTreeWidgetItemIterator it(tree);
            while (*it) { if (QUuid((*it)->data(0, Qt::UserRole + 1).toString()) == id) return *it; ++it; }
            return nullptr;
        };
        QTreeWidgetItem* it = find(vec);
        QVERIFY(it);
        QCOMPARE(it->checkState(2), Qt::Unchecked);
        it->setCheckState(2, Qt::Checked);
        QVERIFY(a->isLayer3D(vec));
        QVERIFY(find(vec)->checkState(2) == Qt::Checked);
        find(vec)->setCheckState(2, Qt::Unchecked);       // unchecking must not crash and must restore the layer
        QVERIFY(!a->isLayer3D(vec));
        QApplication::processEvents();
        QVERIFY(find(vec)->checkState(2) == Qt::Unchecked);
        // turning it off from the inspector keeps the column in sync
        a->makeLayer3D(vec);
        QApplication::processEvents();
        QVERIFY(find(vec)->checkState(2) == Qt::Checked);
        a->removeLayer3D(vec);
        QApplication::processEvents();
        QVERIFY(find(vec)->checkState(2) == Qt::Unchecked);
    }

    void transformFieldsCanBeDraggedWithTheMouse() {
        ScrubSpinBox sb; sb.setRange(-100, 100); sb.setSingleStep(1); sb.setDecimals(2); sb.setValue(10);
        sb.show(); sb.activateWindow(); QTest::qWaitForWindowActive(&sb); sb.clearFocus(); sb.findChild<QLineEdit*>()->clearFocus();
        int changes = 0;
        connect(&sb, &QDoubleSpinBox::valueChanged, this, [&] { ++changes; });
        QWidget* le = sb.findChild<QLineEdit*>();
        QMouseEvent pr(QEvent::MouseButtonPress, QPointF(10, 5), le->mapToGlobal(QPointF(10, 5)), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(le, &pr);
        QMouseEvent mv(QEvent::MouseMove, QPointF(30, 5), le->mapToGlobal(QPointF(30, 5)), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(le, &mv);
        QCOMPARE(sb.value(), 10 + 20.0);
        QVERIFY(changes > 0);
        sb.scrubBy(20, Qt::ShiftModifier);
        QCOMPARE(sb.value(), 12.0);
        sb.scrubBy(2, Qt::ControlModifier);
        QCOMPARE(sb.value(), 30.0);
        QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(30, 5), le->mapToGlobal(QPointF(30, 5)), Qt::LeftButton, Qt::NoButton, {});
        QApplication::sendEvent(le, &rl);
        QVERIFY(!le->hasFocus());                  // a drag does not start text editing
        QTest::mouseClick(le, Qt::LeftButton, {}, QPoint(10, 5));   // a plain click does
        QVERIFY(le->hasFocus());
    }

    void workspacesComeBackIdenticallyWhateverWasShownBefore() {
        auto snap = [&] {
            QStringList out;
            for (QDockWidget* d : w->findChildren<QDockWidget*>())
                out << QString("%1:%2:%3:%4").arg(d->objectName()).arg(d->isVisible()).arg(d->isVisible() ? int(w->dockWidgetArea(d)) : 0).arg(d->isFloating());
            out.sort();
            return out;
        };
        QMap<QString, QStringList> first;
        for (const QString& n : WorkspaceManager::presetNames()) { w->workspaces()->apply(n); QApplication::processEvents(); first[n] = snap(); }
        // go around in a different order, twice, with a 3D panel popped up in between: every workspace must look the same as the first time
        for (int round = 0; round < 2; ++round)
            for (const QString& n : {QString("Animation"), QString("Drawing"), QString("3D"), QString("Illustration"), QString("Drawing"), QString("Animation")}) {
                w->workspaces()->apply(n); QApplication::processEvents();
                QCOMPARE(snap(), first[n]);
            }
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
