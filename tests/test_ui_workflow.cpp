#include <QtTest>
#include <QPainter>
#include <QTabletEvent>

#include "TestEnv.h"
#include "Core/Settings.h"
#include "Graphics/Compositor.h"
#include "UI/MainWindow.h"

using namespace mynd;

class TestUiWorkflow : public QObject {
    Q_OBJECT
    std::unique_ptr<MainWindow> w;
    CanvasView* cv = nullptr;
    EditorSession* s = nullptr;
    QTemporaryDir dir;

    QPoint view(double x, double y) { return cv->docToView().map(QPointF(x, y)).toPoint(); }
    void drag(const QList<QPointF>& docPts, Qt::KeyboardModifiers m = {}) {
        QTest::mousePress(cv, Qt::LeftButton, m, view(docPts[0].x(), docPts[0].y()));
        for (int i = 1; i < docPts.size(); ++i) QTest::mouseMove(cv, view(docPts[i].x(), docPts[i].y()));
        QTest::mouseRelease(cv, Qt::LeftButton, m, view(docPts.last().x(), docPts.last().y()));
    }
    void click(double x, double y) { QTest::mouseClick(cv, Qt::LeftButton, {}, view(x, y)); }
    int alpha(const Layer* l, int x, int y) { return qAlpha(l->raster.pixel(x, y)); }

private slots:
    void init() {
        w = std::make_unique<MainWindow>();
        CompositionSettings cs; cs.width = 400; cs.height = 300;
        w->newDocument(cs);
        w->resize(1300, 800);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        cv = w->canvas(); s = w->session();
        cv->actualPixels();
        cv->panBy({0, 0});
        QApplication::processEvents();
    }
    void cleanup() { w->document()->undoStack()->clear(); w->document()->markSaved(); w->close(); w.reset(); }

    void startsWithOneRasterLayerAndCleanState() {
        QCOMPARE(w->document()->layerCount(), 1);
        QVERIFY(!w->document()->isModified());
        QVERIFY(s->activeLayer());
        QCOMPARE(s->activeLayer()->type, LayerType::Raster);
    }

    void drawingWithBrushPaintsAndUndoes() {
        s->setForeground(QColor(220, 0, 0));
        s->brushFor(ToolId::Brush).size = 14; s->brushFor(ToolId::Brush).hardness = 1;
        s->setTool(ToolId::Brush);
        drag({{50, 100}, {120, 100}, {200, 130}});
        Layer* l = s->activeLayer();
        QCOMPARE(alpha(l, 120, 100), 255);
        QCOMPARE(QColor(l->raster.pixel(120, 100)), QColor(220, 0, 0));
        QCOMPARE(alpha(l, 120, 150), 0);
        QVERIFY(w->document()->isModified());
        // The on-screen composite shows the stroke.
        QCOMPARE(cv->compositeImage().pixelColor(120, 100), QColor(220, 0, 0));
        w->document()->undoStack()->undo();
        QCOMPARE(alpha(l, 120, 100), 0);
        QCOMPARE(cv->compositeImage().pixelColor(120, 100), QColor(Qt::white));
        w->document()->undoStack()->redo();
        QCOMPARE(alpha(l, 120, 100), 255);
    }

    void tabletPressureAffectsStrokeWidth() {
        s->setForeground(Qt::black);
        BrushSettings& b = s->brushFor(ToolId::Brush);
        b.size = 40; b.hardness = 1; b.pressureSize = true;
        s->setTool(ToolId::Brush);
        QPointingDevice dev("test-pen", 1, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        auto send = [&](QEvent::Type t, QPointF doc, double pressure, Qt::MouseButtons btns) {
            const QPointF v = cv->docToView().map(doc);
            QTabletEvent e(t, &dev, v, cv->mapToGlobal(v.toPoint()), pressure, 0, 0, 0, 0, 0, Qt::NoModifier,
                           t == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, btns);
            QApplication::sendEvent(cv, &e);
        };
        send(QEvent::TabletPress, {100, 100}, 0.25, Qt::LeftButton);
        send(QEvent::TabletRelease, {100, 100}, 0.25, Qt::NoButton);
        send(QEvent::TabletPress, {300, 200}, 1.0, Qt::LeftButton);
        send(QEvent::TabletRelease, {300, 200}, 1.0, Qt::NoButton);
        Layer* l = s->activeLayer();
        QCOMPARE(alpha(l, 100 + 8, 100), 0);       // light pressure: radius 5
        QCOMPARE(alpha(l, 100 + 3, 100), 255);
        QCOMPARE(alpha(l, 300 + 15, 200), 255);    // full pressure: radius 20
    }

    void vectorShapeToolsCreateEditableShapesOnAnAutoCreatedLayer() {
        s->setTool(ToolId::Rectangle);
        drag({{40, 40}, {160, 120}});
        Layer* v = s->activeLayer();
        QCOMPARE(v->type, LayerType::Vector);              // layer was created automatically
        QCOMPARE(v->shapes.size(), 1);
        { const QRectF b = v->shapes[0].bounds().adjusted(2, 2, -2, -2); QVERIFY(qAbs(b.width() - 120) <= 1 && qAbs(b.height() - 80) <= 1 && qAbs(b.x() - 40) <= 1 && qAbs(b.y() - 40) <= 1); }  // integer mouse positions: +-0.5px
        QCOMPARE(s->selectedShapes().size(), 1);
        s->setTool(ToolId::Ellipse);
        drag({{200, 40}, {260, 100}}, Qt::ShiftModifier);   // shift = circle
        QCOMPARE(v->shapes.size(), 2);
        QCOMPARE(v->shapes[1].path().boundingRect().size().width(), v->shapes[1].path().boundingRect().size().height());
        s->setTool(ToolId::Polygon); s->polygonSides = 7;
        drag({{300, 200}, {340, 200}});
        QCOMPARE(v->shapes[2].nodes.size(), 7);
        s->setTool(ToolId::Line);
        drag({{20, 250}, {120, 250}});
        QCOMPARE(v->shapes[3].nodes.size(), 2);
        QVERIFY(!v->shapes[3].closed);
        QCOMPARE(cv->compositeImage().pixelColor(100, 80), s->style().fill);   // rect interior filled
        for (int i = 0; i < 4; ++i) w->document()->undoStack()->undo();
        QCOMPARE(v->shapes.size(), 0);
    }

    void selectMoveAndGroupShapes() {
        s->setTool(ToolId::Rectangle);
        drag({{40, 40}, {100, 100}});
        drag({{150, 40}, {210, 100}});
        Layer* v = s->activeLayer();
        const QPointF second0 = v->shapes[1].nodes[0].pos;
        const QPointF first0 = v->shapes[0].nodes[0].pos;
        s->setTool(ToolId::Select);
        click(70, 70);
        QCOMPARE(s->selectedShapes().size(), 1);
        drag({{70, 70}, {90, 95}});                              // move by (20, 25)
        QVERIFY(QLineF(v->shapes[0].nodes[0].pos, QPointF(60, 65)).length() <= 1.0);
        QVERIFY(QLineF(v->shapes[1].nodes[0].pos, second0).length() < 1e-9);   // untouched
        QCOMPARE(w->document()->undoStack()->undoText(), QString("Move Shapes"));
        w->document()->undoStack()->undo();
        QVERIFY(QLineF(v->shapes[0].nodes[0].pos, first0).length() < 1e-9);
        // Marquee selects both; group; clicking one then selects both; moving moves both.
        drag({{10, 10}, {260, 150}});
        QCOMPARE(s->selectedShapes().size(), 2);
        QTest::keyClick(w.get(), Qt::Key_G, Qt::ControlModifier);
        QVERIFY(!v->shapes[0].groupId.isNull());
        QCOMPARE(v->shapes[0].groupId, v->shapes[1].groupId);
        s->clearSelection();
        click(70, 70);
        QCOMPARE(s->selectedShapes().size(), 2);
        drag({{70, 70}, {80, 70}});
        QVERIFY(QLineF(v->shapes[0].nodes[0].pos, QPointF(50, 40)).length() <= 1.0);
        QVERIFY(QLineF(v->shapes[1].nodes[0].pos, QPointF(160, 40)).length() <= 1.0);
        QVERIFY(QLineF(v->shapes[1].nodes[0].pos - v->shapes[0].nodes[0].pos, QPointF(110, 0)).length() < 1e-9);  // moved together
    }

    void penToolBuildsBezierPathAndDirectSelectEditsIt() {
        s->setTool(ToolId::Pen);
        click(50, 200);
        drag({{150, 100}, {190, 100}});                // click-drag = smooth node with handles
        click(250, 200);
        QTest::keyClick(cv, Qt::Key_Return);
        Layer* v = s->activeLayer();
        QCOMPARE(v->type, LayerType::Vector);
        QCOMPARE(v->shapes.size(), 1);
        const VectorShape& sh = v->shapes[0];
        QCOMPARE(sh.nodes.size(), 3);
        QVERIFY(!sh.closed);
        QVERIFY(sh.nodes[1].smooth);
        QCOMPARE(sh.nodes[1].out, QPointF(40, 0));
        QCOMPARE(sh.nodes[1].in, QPointF(-40, 0));
        // Direct selection: drag the middle anchor, then its handle.
        s->setTool(ToolId::DirectSelect);
        click(150, 100);                                // picks the path
        QCOMPARE(s->selectedShapes().size(), 1);
        drag({{150, 100}, {150, 60}});                  // moves the anchor
        QVERIFY(QLineF(v->shapes[0].nodes[1].pos, QPointF(150, 60)).length() <= 1.0);
        QCOMPARE(v->shapes[0].nodes[1].out, QPointF(40, 0));  // handle offset preserved
        drag({{190, 60}, {190, 20}});                   // moves out-handle; smooth => mirrored in-handle
        QVERIFY(QLineF(v->shapes[0].nodes[1].out, QPointF(40, -40)).length() <= 1.0);
        QVERIFY(QLineF(v->shapes[0].nodes[1].in, -v->shapes[0].nodes[1].out).length() < 1e-9);  // mirrored
        w->document()->undoStack()->undo();
        QCOMPARE(v->shapes[0].nodes[1].out, QPointF(40, 0));
        // Pen: clicking the first node closes the path.
        s->setTool(ToolId::Pen);
        click(300, 50); click(380, 50); click(380, 120); click(300, 50);
        QVERIFY(v->shapes.last().closed);
        QCOMPARE(v->shapes.last().nodes.size(), 3);
    }

    void bucketAndEyedropper() {
        // Draw a closed outline with the pencil, bucket-fill inside, then pick the color back.
        s->setForeground(Qt::black);
        auto& pb = s->brushFor(ToolId::Pencil); pb.size = 3;
        s->setTool(ToolId::Pencil);
        drag({{100, 100}, {200, 100}, {200, 200}, {100, 200}, {100, 100}});
        s->setForeground(QColor(10, 200, 50));
        s->bucketTolerance = 0;
        s->setTool(ToolId::Bucket);
        click(150, 150);
        QCOMPARE(QColor(s->activeLayer()->raster.pixel(150, 150)), QColor(10, 200, 50));
        QCOMPARE(alpha(s->activeLayer(), 50, 50), 0);
        s->setForeground(Qt::white);
        s->setTool(ToolId::Eyedropper);
        click(150, 150);
        QCOMPARE(s->foreground(), QColor(10, 200, 50));
    }

    void layerPanelOperationsAreUndoable() {
        Document* d = w->document();
        const QUuid first = s->activeLayerId();
        w->layerPanel()->groupActive();
        QCOMPARE(d->layerCount(), 2);
        QVERIFY(d->findLayer(first)->parent != d->root());
        // Rename + hide through the tree widget like a user would.
        auto* tree = w->layerPanel()->tree();
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem* grp = tree->topLevelItem(0);
        QCOMPARE(grp->childCount(), 1);
        QTreeWidgetItem* leaf = grp->child(0);
        leaf->setText(0, "Sketch");
        QApplication::processEvents();   // the panel rebuilds itself asynchronously after an inline edit
        QCOMPARE(d->findLayer(first)->props.name, QString("Sketch"));
        s->setActiveLayer(first);   // grouping selects the new group; paint on the leaf
        s->setForeground(Qt::black); s->setTool(ToolId::Pencil);
        drag({{10, 10}, {60, 10}});
        QCOMPARE(cv->compositeImage().pixelColor(30, 10), QColor(Qt::black));
        leaf = tree->topLevelItem(0)->child(0);
        leaf->setCheckState(0, Qt::Unchecked);
        QApplication::processEvents();
        QVERIFY(!d->findLayer(first)->props.visible);
        QCOMPARE(cv->compositeImage().pixelColor(30, 10), QColor(Qt::white));     // composite follows
        d->undoStack()->undo();
        QCOMPARE(cv->compositeImage().pixelColor(30, 10), QColor(Qt::black));
        // New vector layer goes above the active one; reorder up/down.
        s->setActiveLayer(first);
        w->layerPanel()->ungroupActive();   // active is a leaf: no-op
        s->addLayerAboveActive(LayerType::Vector, "V");
        QCOMPARE(d->layerCount(), 3);
        const QUuid vId = s->activeLayerId();
        w->layerPanel()->moveActive(-1);
        QCOMPARE(s->activeLayerId(), vId);                  // moving a layer must not change the active layer
        QCOMPARE(d->indexInParent(s->activeLayer()), 0);
        w->layerPanel()->deleteActive();
        QCOMPARE(d->layerCount(), 2);
        d->undoStack()->undo();
        QCOMPARE(d->layerCount(), 3);
        // Locked layers refuse edits and say why.
        LayerProps p = d->findLayer(first)->props; p.locked = true; d->setLayerProps(first, p);
        s->setActiveLayer(first);
        QSignalSpy msg(s, &EditorSession::message);
        s->setTool(ToolId::Brush);
        drag({{200, 200}, {250, 250}});
        QVERIFY(msg.count() >= 1);
        QCOMPARE(alpha(d->findLayer(first), 225, 225), 0);
    }

    void canvasNavigation() {
        const QPointF d(123, 77);
        QPointF back = cv->viewToDoc(cv->docToView().map(d));
        QVERIFY(QLineF(back, d).length() < 1e-6);
        const QPointF anchor(300, 300);
        const QPointF docUnder = cv->viewToDoc(anchor);
        cv->zoomAt(2.0, anchor);
        QVERIFY(QLineF(cv->viewToDoc(anchor), docUnder).length() < 1e-6);   // zoom keeps the point under the cursor
        QVERIFY(qFuzzyCompare(cv->zoom(), 2.0));
        cv->rotateBy(90);
        QVERIFY(qFuzzyCompare(cv->rotation() + 1, 91.0));
        QVERIFY(QLineF(cv->viewToDoc(cv->docToView().map(d)), d).length() < 1e-6);
        // Drawing still lands in the right place on a rotated + zoomed canvas.
        s->setForeground(Qt::black); s->brushFor(ToolId::Pencil).size = 5; s->setTool(ToolId::Pencil);
        drag({{100, 100}, {110, 100}});
        QVERIFY(alpha(s->activeLayer(), 105, 100) == 255);
        cv->resetRotation();
        QCOMPARE(cv->rotation(), 0.0);
        cv->fitToView();
        QVERIFY(cv->zoom() > 0.5 && cv->zoom() < 4);
        const QRectF mapped = cv->docToView().mapRect(QRectF(0, 0, 400, 300));
        QVERIFY(QRectF(cv->rect()).contains(mapped));
        cv->setZoom(8); QCOMPARE(cv->zoom(), 8.0);
        cv->setZoom(1000); QVERIFY(cv->zoom() <= 64.0);
    }

    void snappingToGridAndGuides() {
        cv->actualPixels();
        s->snapEnabled = true; s->gridVisible = true;
        w->document()->compMutable().gridSize = 50;
        QCOMPARE(cv->snap({102.5, 197}), QPointF(100, 200));
        QCOMPARE(cv->snap({125, 125}), QPointF(125, 125));       // far from any grid line
        w->document()->compMutable().guides.append({Qt::Vertical, 133});
        QCOMPARE(cv->snap({130, 125}).x(), 133.0);
        s->snapEnabled = false;
        QCOMPARE(cv->snap({102.5, 197}), QPointF(102.5, 197));
    }

    void savingReopeningAndExportingRoundTrips() {
        s->setForeground(QColor(30, 60, 200)); s->setTool(ToolId::Brush);
        drag({{20, 20}, {300, 250}});
        s->setTool(ToolId::Ellipse);
        drag({{150, 60}, {250, 140}});
        const QImage before = cv->compositeImage();
        const QString path = dir.filePath("work.studio");
        QString err;
        QVERIFY2(w->saveFile(path, &err), qPrintable(err));
        QVERIFY(!w->document()->isModified());
        QVERIFY(w->windowTitle().startsWith("work.studio"));
        // New document, then reopen: everything is still there and still editable.
        w->newDocument({});
        QVERIFY(w->openFile(path, &err));
        QCOMPARE(w->document()->layerCount(), 2);
        QCOMPARE(cv->compositeImage(), before);
        Layer* vec = nullptr;
        Document::forEachLayer(w->document()->root(), [&](Layer* l) { if (l->type == LayerType::Vector) vec = l; });
        QVERIFY(vec && vec->shapes.size() == 1 && vec->shapes[0].nodes.size() == 4);
        s->setActiveLayer(vec->id);
        s->setTool(ToolId::Select);
        click(200, 100);
        QCOMPARE(s->selectedShapes().size(), 1);
        // Export flattens to PNG matching the composite.
        const QString png = dir.filePath("out.png");
        QVERIFY(w->exportImage(png, &err));
        QImage ex(png);
        QCOMPARE(ex.size(), QSize(400, 300));
        QCOMPARE(ex.convertToFormat(QImage::Format_ARGB32_Premultiplied), Compositor::render(*w->document()));
        QVERIFY(!w->exportImage(dir.filePath("no/such/dir/x.png"), &err));
    }

    void gradientToolTargetsShapeUnderDragStart() {
        s->setTool(ToolId::Rectangle); drag({{40, 40}, {120, 120}});
        s->setTool(ToolId::Ellipse); drag({{200, 40}, {280, 120}});       // ellipse is now selected
        s->setForeground(Qt::black); s->setBackground(Qt::white);
        s->setTool(ToolId::Gradient);
        drag({{45, 80}, {115, 80}});                                       // dragged over the *rectangle*
        Layer* v = s->activeLayer();
        QCOMPARE(v->shapes[0].fill.type, Paint::LinearGradient);
        QCOMPARE(v->shapes[1].fill.type, Paint::Solid);
        const QColor left = cv->compositeImage().pixelColor(50, 80), right = cv->compositeImage().pixelColor(110, 80);
        QVERIFY(left.red() < right.red());
        w->document()->undoStack()->undo();
        QCOMPARE(v->shapes[0].fill.type, Paint::Solid);
    }

    void workspacesRearrangePanelsAndPersistCustomLayouts() {
        auto* ws = w->workspaces();
        QVERIFY(ws->apply("Illustration"));
        auto vis = [&](const QString& dock) { return w->findChild<QDockWidget*>("dock." + dock)->isVisible(); };
        QVERIFY(vis("history"));
        QVERIFY(ws->apply("Drawing"));
        QVERIFY(!vis("history"));
        QVERIFY(vis("layers") && vis("options") && vis("style"));
        // The project itself is untouched by workspace changes.
        QCOMPARE(w->document()->layerCount(), 1);
        w->findChild<QDockWidget*>("dock.history")->show();
        ws->saveCurrentAs("My Layout");
        QVERIFY(ws->customNames().contains("My Layout"));
        ws->apply("Illustration");
        QVERIFY(ws->apply("My Layout"));
        QVERIFY(vis("history"));
        QCOMPARE(Settings_lastWorkspace(), QString("My Layout"));
        QVERIFY(ws->deleteCustom("My Layout"));
        QVERIFY(!ws->allNames().contains("My Layout"));
        QVERIFY(!ws->apply("Nope"));
    }

    void shortcutsSwitchToolsAndAdjustBrushSize() {
        QTest::keyClick(w.get(), Qt::Key_E);
        QCOMPARE(s->tool(), ToolId::Eraser);
        QTest::keyClick(w.get(), Qt::Key_P);
        QCOMPARE(s->tool(), ToolId::Pen);
        QTest::keyClick(w.get(), Qt::Key_B);
        QCOMPARE(s->tool(), ToolId::Brush);
        const double sz = s->brush(ToolId::Brush).size;
        QTest::keyClick(w.get(), Qt::Key_BracketRight);
        QVERIFY(s->brush(ToolId::Brush).size > sz);
        QTest::keyClick(w.get(), Qt::Key_BracketLeft);
        QTest::keyClick(w.get(), Qt::Key_BracketLeft);
        QVERIFY(s->brush(ToolId::Brush).size < sz);
        s->setForeground(Qt::red); s->setBackground(Qt::blue);
        QTest::keyClick(w.get(), Qt::Key_X);
        QCOMPARE(s->foreground(), QColor(Qt::blue));
    }

private:
    static QString Settings_lastWorkspace() { return mynd::settings::lastWorkspace(); }
};

QTEST_MAIN(TestUiWorkflow)
#include "test_ui_workflow.moc"
