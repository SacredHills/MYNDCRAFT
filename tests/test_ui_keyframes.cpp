#include <QtTest>
#include <QMenu>
#include <QToolBar>
#include <QPainter>

#include "TestEnv.h"
#include "Core/Exporter.h"
#include "Core/Settings.h"
#include "UI/MainWindow.h"
#include "UI/PropertiesPanel.h"
#include "UI/TimelinePanel.h"
#include "UI/Workspaces.h"

using namespace mynd;

class TestUiKeyframes : public QObject {
    Q_OBJECT
    std::unique_ptr<MainWindow> w;
    CanvasView* cv = nullptr;
    EditorSession* s = nullptr;
    Document* doc = nullptr;
    QUuid layerId;

    QPoint view(double x, double y) { return cv->docToView().map(QPointF(x, y)).toPoint(); }
    void drag(QPointF a, QPointF b, Qt::KeyboardModifiers m = {}) {
        QTest::mousePress(cv, Qt::LeftButton, m, view(a.x(), a.y()));
        QTest::mouseMove(cv, view((a.x() + b.x()) / 2, (a.y() + b.y()) / 2));
        QTest::mouseMove(cv, view(b.x(), b.y()));
        QTest::mouseRelease(cv, Qt::LeftButton, m, view(b.x(), b.y()));
    }
    Layer* L() { return doc->findLayer(layerId); }
    PropertiesPanel* panel() { return w->findChild<PropertiesPanel*>(); }
    TimelineView* tv() { return w->findChild<TimelinePanel*>()->view(); }
    QColor px(int frame, int x, int y) {
        Compositor::Options o; o.frame = frame;
        return Compositor::render(*doc, doc->canvasRect(), o).pixelColor(x, y);
    }
    // Keys of a property as "frame:x" strings.
    QStringList keys(const QString& prop) {
        QStringList out;
        if (!L()->tracks.contains(prop)) return out;
        for (const Keyframe& k : L()->tracks[prop].keys) out << QString("%1:%2").arg(k.frame).arg(k.value.c[0]);
        return out;
    }

private slots:
    void init() {
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        w = std::make_unique<MainWindow>();
        CompositionSettings cs; cs.width = 400; cs.height = 300; cs.durationFrames = 30; cs.background = Qt::white;
        w->newDocument(cs, false);
        w->resize(1500, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->workspaces()->apply("Animation");
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        QApplication::processEvents();
        cv = w->canvas(); s = w->session(); doc = w->document();
        layerId = s->addLayerAboveActive(LayerType::Raster, "Square");
        { QPainter p(&L()->raster); p.fillRect(100, 100, 60, 60, Qt::red); }
        doc->undoStack()->clear();
        cv->actualPixels();
        cv->panBy({0, 0});
        QApplication::processEvents();
    }
    void cleanup() {
        doc->undoStack()->clear(); doc->markSaved(); w->close(); w.reset();
    }

    void windowCannotShrinkUntilPanelsOverlapAndEveryWorkspaceStaysUsable() {
        w->resize(250, 300);
        QApplication::processEvents();
        QVERIFY(w->width() >= 1000 && w->height() >= 650);
        for (const QString& n : {QString("Drawing"), QString("Illustration"), QString("Animation"), QString("Drawing")}) {
            QVERIFY(w->workspaces()->apply(n));
            QApplication::processEvents();
            QVERIFY2(w->canvas()->width() >= 200, qPrintable(n + " canvas=" + QString::number(w->canvas()->width()) + " win=" + QString::number(w->width())));              // canvas keeps real room
            const QRect tools = w->findChild<QToolBar*>("tools")->geometry();
            for (QDockWidget* d : w->findChildren<QDockWidget*>())
                if (d->isVisible() && !d->isFloating()) QVERIFY2(!d->geometry().intersects(tools), qPrintable(n + " " + d->objectName()));
        }
    }

    // ------------------------------------------------------------ session semantics
    void autoKeyDecidesWhetherAFirstEditCreatesAKeyframe() {
        QVERIFY(!s->autoKey());
        doc->setCurrentFrame(4);
        QVERIFY(s->setProperty(layerId, "transform.position", PropValue::of(30, 10)));
        QVERIFY(keys("transform.position").isEmpty());                          // static value changed
        QCOMPARE(px(0, 135, 135), QColor(255, 255 - 255, 255 - 255).toRgb() == QColor(Qt::red) ? QColor(Qt::red) : QColor(Qt::red));
        QCOMPARE(px(20, 165, 115), QColor(Qt::red));                           // moved on every frame
        s->setAutoKey(true);
        doc->setCurrentFrame(10);
        QVERIFY(s->setProperty(layerId, "transform.position", PropValue::of(80, 10)));
        QCOMPARE(L()->tracks["transform.position"].keys.size(), 1);            // key at frame 10
        QCOMPARE(L()->tracks["transform.position"].keys[0].frame, 10);
        s->setAutoKey(false);
        doc->setCurrentFrame(20);
        QVERIFY(s->setProperty(layerId, "transform.position", PropValue::of(0, 0)));
        QCOMPARE(L()->tracks["transform.position"].keys.size(), 2);            // animated properties always key
        QCOMPARE(s->propertyValue(layerId, "transform.position").c[0], 0.0);
        doc->setCurrentFrame(15);
        QCOMPARE(s->propertyValue(layerId, "transform.position").c[0], 40.0);  // halfway between 80 and 0
        // each edit is one undo step
        doc->undoStack()->undo();
        QCOMPARE(L()->tracks["transform.position"].keys.size(), 1);
        // a locked layer refuses
        LayerProps lp = L()->props; lp.locked = true; doc->setLayerProps(layerId, lp);
        QVERIFY(!s->setProperty(layerId, "transform.position", PropValue::of(1, 1)));
        QVERIFY(!s->toggleAnimated(layerId, "opacity"));
        QVERIFY(!s->toggleKeyHere(layerId, "opacity"));
    }

    void stopwatchAndKeyToggleAddRemoveKeysAndNavigationJumps() {
        doc->setCurrentFrame(5);
        QVERIFY(s->toggleAnimated(layerId, "opacity"));
        QCOMPARE(keys("opacity"), QStringList{"5:100"});
        doc->setCurrentFrame(12);
        QVERIFY(s->setProperty(layerId, "opacity", PropValue::of(20)));
        QCOMPARE(keys("opacity"), (QStringList{"5:100", "12:20"}));
        doc->setCurrentFrame(20);
        QVERIFY(s->toggleKeyHere(layerId, "opacity"));                          // add a key holding the current value
        QCOMPARE(keys("opacity"), (QStringList{"5:100", "12:20", "20:20"}));
        QVERIFY(s->toggleKeyHere(layerId, "opacity"));                          // toggled off again
        QCOMPARE(keys("opacity").size(), 2);
        doc->setCurrentFrame(0);
        QVERIFY(s->gotoKey(+1)); QCOMPARE(doc->currentFrame(), 5);
        QVERIFY(s->gotoKey(+1)); QCOMPARE(doc->currentFrame(), 12);
        QVERIFY(!s->gotoKey(+1));
        QVERIFY(s->gotoKey(-1)); QCOMPARE(doc->currentFrame(), 5);
        QVERIFY(!s->gotoKey(-1));
        // stopwatch off freezes the value seen at the playhead and removes every key
        doc->setCurrentFrame(8);
        const double at8 = s->propertyValue(layerId, "opacity").c[0];
        QVERIFY(s->toggleAnimated(layerId, "opacity"));
        QVERIFY(keys("opacity").isEmpty());
        QCOMPARE(s->propertyValue(layerId, "opacity").c[0], at8);
        // removing the last key with the key button also stops animating
        QVERIFY(s->toggleAnimated(layerId, "opacity"));
        QVERIFY(s->toggleKeyHere(layerId, "opacity"));
        QVERIFY(!L()->tracks.contains("opacity") || !L()->tracks["opacity"].animated());
    }

    void keyframeSelectionMoveEaseCopyPasteAndDeleteAreUndoable() {
        s->setAutoKey(true);
        for (int f : {0, 10, 20}) { doc->setCurrentFrame(f); s->setProperty(layerId, "transform.position", PropValue::of(f * 5.0, 0)); }
        s->setAutoKey(false);
        QCOMPARE(keys("transform.position"), (QStringList{"0:0", "10:50", "20:100"}));
        s->setKeySelection({{layerId, "transform.position", 10}, {layerId, "transform.position", 20}});
        const int base = doc->undoStack()->count();
        QVERIFY(s->moveSelectedKeys(3));
        QCOMPARE(keys("transform.position"), (QStringList{"0:0", "13:50", "23:100"}));
        QCOMPARE(doc->undoStack()->count(), base + 1);                          // one step
        QVERIFY(s->isKeySelected(layerId, "transform.position", 13));           // selection follows the move
        QVERIFY(s->moveSelectedKeys(-100));                                     // clamped at frame 0 for the earliest selected
        QCOMPARE(keys("transform.position").first(), QString("0:50"));          // the moved key replaced the one at 0
        doc->undoStack()->undo();
        QCOMPARE(keys("transform.position"), (QStringList{"0:0", "13:50", "23:100"}));
        // ease presets
        s->setKeySelection({{layerId, "transform.position", 0}});
        QVERIFY(s->easeSelectedKeys(EasePreset::EaseInOut));
        QVERIFY(L()->tracks["transform.position"].keys[0].out == Interp::Bezier);
        s->setKeySelection({{layerId, "transform.position", 13}});
        QVERIFY(s->easeSelectedKeys(EasePreset::Hold, false, true));
        doc->setCurrentFrame(20);
        QCOMPARE(s->propertyValue(layerId, "transform.position").c[0], 50.0);   // held from the key at 13 until 23
        // copy + paste onto another layer at the playhead
        s->setKeySelection({{layerId, "transform.position", 0}, {layerId, "transform.position", 13}});
        QVERIFY(s->copyKeys(false));
        const QUuid other = s->addLayerAboveActive(LayerType::Raster, "Other");
        doc->setCurrentFrame(5);
        QVERIFY(s->pasteKeys());
        const auto& ok = doc->findLayer(other)->tracks["transform.position"].keys;
        QCOMPARE(ok.size(), 2);
        QCOMPARE(ok[0].frame, 5); QCOMPARE(ok[1].frame, 18);                    // spacing preserved
        QVERIFY(ok[1].out == Interp::Hold);                                     // interpolation travels with the key
        QCOMPARE(s->selectedKeys().size(), 2);
        // cut removes the originals
        s->setActiveLayer(layerId);
        s->setKeySelection({{layerId, "transform.position", 23}});
        QVERIFY(s->copyKeys(true));
        QCOMPARE(keys("transform.position").size(), 2);
        QVERIFY(s->selectedKeys().isEmpty());
        // delete selected: the property stays at the last key's value when everything is removed
        s->setKeySelection({{layerId, "transform.position", 0}, {layerId, "transform.position", 13}});
        QVERIFY(s->deleteSelectedKeys());
        QVERIFY(!L()->tracks.contains("transform.position") || !L()->tracks["transform.position"].animated());
        doc->undoStack()->undo();
        QCOMPARE(keys("transform.position").size(), 2);
        // frames and keyframes are never selected together
        s->setKeySelection({{layerId, "transform.position", 0}});
        s->setFrameSelection(layerId, 1, 2);
        QVERIFY(s->selectedKeys().isEmpty());
    }

    // ------------------------------------------------------------ Properties panel
    void propertiesPanelEditsAnimatesAndFollowsThePlayhead() {
        PropertiesPanel* p = panel();
        QVERIFY(p);
        // editing a field changes the layer
        p->field("transform.position", 0)->setValue(50);
        p->field("transform.position", 0)->editingFinished();
        QCOMPARE(L()->tracks["transform.position"].base.c[0], 50.0);
        QCOMPARE(px(0, 175, 130), QColor(Qt::red));
        // stopwatch turns animation on; the key button reflects the key at the playhead
        QVERIFY(!p->stopwatch("opacity")->isChecked());
        p->stopwatch("opacity")->click();
        QVERIFY(p->stopwatch("opacity")->isChecked());
        QCOMPARE(keys("opacity"), QStringList{"0:100"});
        doc->setCurrentFrame(10);
        p->field("opacity", 0)->setValue(0);
        QCOMPARE(keys("opacity"), (QStringList{"0:100", "10:0"}));              // animated: edit keys without auto-key
        doc->setCurrentFrame(5);
        QCOMPARE(p->field("opacity", 0)->value(), 50.0);                       // panel shows the interpolated value
        QVERIFY(qAbs(px(5, 170, 130).green() - 127) <= 3);                      // and the canvas renders it
        // arrows jump between keyframes of that property
        doc->setCurrentFrame(3);
        p->keyButton("opacity")->click();
        QCOMPARE(keys("opacity").size(), 3);
        doc->setCurrentFrame(0);
        QTest::mouseClick(p->findChildren<QToolButton*>().at(0), Qt::LeftButton);   // (harmless: the first stopwatch)
        // auto-key checkbox mirrors the session
        auto* cb = p->findChild<QCheckBox*>();
        QVERIFY(cb && !cb->isChecked());
        s->setAutoKey(true);
        QVERIFY(cb->isChecked());
        cb->setChecked(false);
        QVERIFY(!s->autoKey());
    }

    void scaleKeepsProportionsAndTypingMergesIntoOneUndoStep() {
        PropertiesPanel* p = panel();
        const int base = doc->undoStack()->count();
        p->field("transform.scale", 0)->setValue(200);
        QCOMPARE(p->field("transform.scale", 1)->value(), 200.0);              // link is on by default
        p->field("transform.scale", 0)->setValue(150);
        QCOMPARE(L()->tracks["transform.scale"].base.c[1], 150.0);
        QCOMPARE(doc->undoStack()->count(), base + 1);                          // quick successive edits merged
        doc->undoStack()->undo();
        QVERIFY(!L()->tracks.contains("transform.scale"));
        p->field("transform.rotation", 0)->setValue(45);
        QCOMPARE(L()->tracks["transform.rotation"].base.c[0], 45.0);
        // locked layers cannot be edited from the panel
        LayerProps lp = L()->props; lp.locked = true; doc->setLayerProps(layerId, lp);
        QApplication::processEvents();
        QVERIFY(!p->field("transform.rotation", 0)->isEnabled());
    }

    // ------------------------------------------------------------ timeline
    void timelineExpandsLayersAndEditsKeyframes() {
        s->setAutoKey(true);
        for (int f : {0, 10, 20}) { doc->setCurrentFrame(f); s->setProperty(layerId, "transform.position", PropValue::of(f * 2.0, 0)); }
        s->setAutoKey(false);
        TimelineView* t = tv();
        t->setCellWidth(20);
        const int layerRow = t->rowOfLayer(layerId);
        QVERIFY(layerRow >= 0);
        QCOMPARE(t->rowOfProperty(layerId, "transform.position"), -1);          // collapsed
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, t->twirlPoint(layerRow));
        QVERIFY(t->isExpanded(layerId));
        const int pr = t->rowOfProperty(layerId, "transform.position");
        QVERIFY(pr == layerRow + 2);                                            // anchor, position, ...
        QApplication::processEvents();
        // click a diamond: selects it and moves the playhead there
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, t->cellCenter(10, pr));
        QCOMPARE(s->selectedKeys().size(), 1);
        QCOMPARE(doc->currentFrame(), 10);
        QVERIFY(s->isKeySelected(layerId, "transform.position", 10));
        // Ctrl+click adds, Ctrl+click again removes
        QTest::mouseClick(t->viewport(), Qt::LeftButton, Qt::ControlModifier, t->cellCenter(20, pr));
        QCOMPARE(s->selectedKeys().size(), 2);
        QTest::mouseClick(t->viewport(), Qt::LeftButton, Qt::ControlModifier, t->cellCenter(20, pr));
        QCOMPARE(s->selectedKeys().size(), 1);
        // drag the selected key 4 frames right: one undo step
        const int base = doc->undoStack()->count();
        QTest::mousePress(t->viewport(), Qt::LeftButton, {}, t->cellCenter(10, pr));
        QTest::mouseMove(t->viewport(), t->cellCenter(12, pr));
        QTest::mouseMove(t->viewport(), t->cellCenter(14, pr));
        QTest::mouseRelease(t->viewport(), Qt::LeftButton, {}, t->cellCenter(14, pr));
        QCOMPARE(keys("transform.position"), (QStringList{"0:0", "14:20", "20:40"}));
        QCOMPARE(doc->undoStack()->count(), base + 1);
        // marquee selects every key inside the box
        QTest::mousePress(t->viewport(), Qt::LeftButton, {}, t->cellCenter(16, pr) + QPoint(0, 8));
        QTest::mouseMove(t->viewport(), t->cellCenter(8, pr));
        QTest::mouseMove(t->viewport(), t->cellCenter(0, pr) + QPoint(-6, -10));
        QTest::mouseRelease(t->viewport(), Qt::LeftButton, {}, t->cellCenter(0, pr) + QPoint(-6, -10));
        QCOMPARE(s->selectedKeys().size(), 2);                                  // frames 0 and 14, not 20
        // Delete key removes them
        t->setFocus();
        QTest::keyClick(t, Qt::Key_Delete);
        QCOMPARE(keys("transform.position"), QStringList{"20:40"});
        // clicking an empty property cell clears the key selection and moves the playhead
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, t->cellCenter(3, pr));
        QVERIFY(s->selectedKeys().isEmpty());
        QCOMPARE(doc->currentFrame(), 3);
        // collapse again
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, t->twirlPoint(layerRow));
        QVERIFY(!t->isExpanded(layerId));
    }

    void keyboardShortcutsCopyPasteKeyframesAndPickEasing() {
        s->setAutoKey(true);
        for (int f : {0, 8}) { doc->setCurrentFrame(f); s->setProperty(layerId, "opacity", PropValue::of(f == 0 ? 100 : 0)); }
        s->setAutoKey(false);
        tv()->setExpanded(layerId, true);
        w->activateWindow();
        s->setKeySelection({{layerId, "opacity", 0}, {layerId, "opacity", 8}});
        QTest::keyClick(w.get(), Qt::Key_F9);                                   // Easy Ease
        QVERIFY(L()->tracks["opacity"].keys[0].out == Interp::Bezier);
        QVERIFY(L()->tracks["opacity"].keys[1].in == Interp::Bezier);
        doc->setCurrentFrame(1);
        QVERIFY(L()->tracks["opacity"].valueAt(*findPropertyDef("opacity"), 1).c[0] > 95);   // slow start
        QTest::keyClick(w.get(), Qt::Key_C, Qt::ControlModifier);               // copies keys, not frames
        QVERIFY(!s->keyClip().isEmpty());
        doc->setCurrentFrame(14);
        QTest::keyClick(w.get(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(L()->tracks["opacity"].keys.size(), 4);
        QCOMPARE(L()->tracks["opacity"].keys[2].frame, 14);
        doc->setCurrentFrame(0);
        QTest::keyClick(w.get(), Qt::Key_Period, Qt::ControlModifier);          // next keyframe
        QCOMPARE(doc->currentFrame(), 8);
        QTest::keyClick(w.get(), Qt::Key_Comma, Qt::ControlModifier);
        QCOMPARE(doc->currentFrame(), 0);
        // context menu of a key offers the interpolation presets
        QMenu m;
        w->animation()->fillKeyframeMenu(&m);
        QStringList texts; for (QAction* a : m.actions()) texts << a->text();
        QVERIFY(texts.contains("Copy Keyframes"));
        QVERIFY(texts.contains("Easy Ease (In and Out)") && texts.contains("Hold") && texts.contains("Linear"));
    }

    // ------------------------------------------------------------ Transform Layer tool
    void transformToolMovesWithAutoKeyAndTheResultAnimates() {
        s->setTool(ToolId::LayerTransform);
        s->setAutoKey(true);
        doc->setCurrentFrame(0);
        const int base = doc->undoStack()->count();
        drag({130, 130}, {190, 170});                                           // move by (60, 40)
        QCOMPARE(doc->undoStack()->count(), base + 1);                          // the whole drag is one undo step
        QCOMPARE(keys("transform.position"), QStringList{"0:60"});
        QCOMPARE(px(0, 200, 160), QColor(Qt::red));                             // square now at 160..220 x 140..200
        QCOMPARE(px(0, 110, 110).alpha() > 0 && px(0, 110, 110) != QColor(Qt::red), true);   // white background where it was
        doc->setCurrentFrame(10);
        drag({190, 170}, {230, 170});                                           // +40 more
        QCOMPARE(keys("transform.position"), (QStringList{"0:60", "10:100"}));
        QCOMPARE(px(5, 210, 170), QColor(Qt::red));                             // frame 5: position x = 80
        QCOMPARE(px(5, 300, 170), QColor(Qt::white));
        // the original pixels are never touched
        QCOMPARE(QColor(L()->raster.pixelColor(130, 130)), QColor(Qt::red));
        // undo reverts the last drag only
        doc->undoStack()->undo();
        QCOMPARE(keys("transform.position"), QStringList{"0:60"});
    }

    void transformToolRotatesScalesAndMovesTheAnchorWithoutMovingThePicture() {
        s->setTool(ToolId::LayerTransform);
        // rotate: grab just outside the box, swing 90 degrees clockwise about the anchor (canvas centre)
        drag({90, 90}, {260, 40});
        QVERIFY(qAbs(L()->tracks["transform.rotation"].base.c[0] - 90.0) < 1.5);
        QCOMPARE(px(0, 215, 100), QColor(Qt::red));                             // the square swung round the centre to the top right
        QCOMPARE(px(0, 130, 130), QColor(Qt::white));
        doc->undoStack()->undo();
        QVERIFY(!L()->tracks.contains("transform.rotation"));
        // Shift snaps to 15 degrees
        drag({90, 90}, {200, 20}, Qt::ShiftModifier);
        const double r = L()->tracks["transform.rotation"].base.c[0];
        QVERIFY(qAbs(std::fmod(r, 15.0)) < 1e-6);
        doc->undoStack()->undo();
        // scale from the bottom-right corner handle about the anchor
        drag({160, 160}, {120, 170});
        QVERIFY(qAbs(L()->tracks["transform.scale"].base.c[0] - 200.0) < 8.0);   // pointer positions are whole pixels; 40px lever arm
        QVERIFY(qAbs(L()->tracks["transform.scale"].base.c[1] - 200.0) < 8.0);   // y lever arm is only 10px, so half a pixel is 5 %
        doc->undoStack()->undo();
        // moving the anchor leaves the picture exactly where it was
        s->setProperty(layerId, "transform.rotation", PropValue::of(30));
        s->setProperty(layerId, "transform.scale", PropValue::of(150, 80));
        const QImage before = Compositor::render(*doc, doc->canvasRect(), {});
        const QPointF anchorNow = QPointF(200, 150);                            // default anchor = canvas centre
        drag(anchorNow, {120, 90});
        QVERIFY(L()->tracks.contains("transform.anchor"));
        const QImage after = Compositor::render(*doc, doc->canvasRect(), {});
        int diff = 0;
        for (int y = 0; y < before.height(); ++y) for (int x = 0; x < before.width(); ++x) diff += before.pixel(x, y) != after.pixel(x, y);
        QVERIFY2(diff < 40, qPrintable(QString::number(diff)));                // identical up to resampling noise on the edge
        QVERIFY(L()->tracks["transform.anchor"].base.c[0] < -70);
    }

    void transformToolEscapeCancelsAndLockedLayersAreProtected() {
        s->setTool(ToolId::LayerTransform);
        const int base = doc->undoStack()->count();
        QTest::mousePress(cv, Qt::LeftButton, {}, view(130, 130));
        QTest::mouseMove(cv, view(180, 160));
        QVERIFY(L()->tracks.contains("transform.position"));
        QTest::keyClick(cv, Qt::Key_Escape);
        QTest::mouseRelease(cv, Qt::LeftButton, {}, view(180, 160));
        QVERIFY(!L()->tracks.contains("transform.position"));
        QCOMPARE(doc->undoStack()->index(), base);                              // cancelled: nothing left to undo
        LayerProps lp = L()->props; lp.locked = true; doc->setLayerProps(layerId, lp);
        const int b2 = doc->undoStack()->count();
        drag({130, 130}, {180, 160});
        QCOMPARE(doc->undoStack()->count(), b2);
        QVERIFY(!L()->tracks.contains("transform.position"));
        // an empty layer has nothing to transform
        lp.locked = false; doc->setLayerProps(layerId, lp);
        const QUuid empty = s->addLayerAboveActive(LayerType::Raster, "Empty");
        drag({130, 130}, {180, 160});
        QVERIFY(!doc->findLayer(empty)->tracks.contains("transform.position"));
    }

    void groupsAndVectorLayersTransformToo() {
        const QUuid g = s->addLayerAboveActive(LayerType::Group, "G");
        s->setActiveLayer(layerId);
        doc->moveLayer(layerId, g, 0);
        s->setActiveLayer(g);
        s->setTool(ToolId::LayerTransform);
        drag({130, 130}, {150, 130});
        QVERIFY(doc->findLayer(g)->tracks.contains("transform.position"));
        QCOMPARE(px(0, 170, 130), QColor(Qt::red));
        // vector layer: bounds come from the shapes
        const QUuid v = s->addLayerAboveActive(LayerType::Vector, "V");
        VectorShape r = VectorShape::makeRect(QRectF(20, 20, 50, 50));
        r.fill.type = Paint::Solid; r.fill.color = Qt::blue;
        doc->setShapes(v, {r}, "r");
        QCOMPARE(doc->findLayer(v)->contentBounds(0, doc->canvasSize()).width() >= 50, true);
        drag({40, 40}, {60, 40});
        QVERIFY(doc->findLayer(v)->tracks.contains("transform.position"));
    }

    void keyframesPlayBackThroughThePlaybackEngine() {
        s->setAutoKey(true);
        doc->setCurrentFrame(0); s->setProperty(layerId, "transform.position", PropValue::of(0, 0));
        doc->setCurrentFrame(24); s->setProperty(layerId, "transform.position", PropValue::of(240, 0));
        s->setAutoKey(false);
        doc->setCurrentFrame(0);
        s->playback()->play();
        s->playback()->tick(500000);                                            // 0.5 s at 24 fps = frame 12
        QCOMPARE(doc->currentFrame(), 12);
        QCOMPARE(px(doc->currentFrame(), 100 + 120 + 10, 130), QColor(Qt::red));
        QCOMPARE(px(doc->currentFrame(), 105, 130), QColor(Qt::white));
        s->playback()->pause();
    }

    void exportRendersKeyframedFramesAndNotTheEditorOverlay() {
        s->setAutoKey(true);
        doc->setCurrentFrame(0); s->setProperty(layerId, "opacity", PropValue::of(100));
        doc->setCurrentFrame(4); s->setProperty(layerId, "opacity", PropValue::of(0));
        s->setAutoKey(false);
        QTemporaryDir d;
        exporter::ExportOptions o; o.firstFrame = 0; o.lastFrame = 4;
        QStringList files; QString err;
        QVERIFY2(exporter::writePngSequence(*doc, d.path(), "f", o, &files, &err), qPrintable(err));
        QCOMPARE(QImage(files[0]).pixelColor(130, 130), QColor(Qt::red));
        QCOMPARE(QImage(files[4]).pixelColor(130, 130), QColor(Qt::white));
        const int g2 = QImage(files[2]).pixelColor(130, 130).green();
        QVERIFY(qAbs(g2 - 127) <= 3);
    }

    // Optional: MYND_SHOT=/path/out.png renders the Animation workspace with keyframes for visual review.
    void zzScreenshotForReview() {
        const QString out = qEnvironmentVariable("MYND_SHOT");
        if (out.isEmpty()) QSKIP("set MYND_SHOT to write a screenshot");
        s->setAutoKey(true);
        for (int f : {0, 8, 16, 24}) {
            doc->setCurrentFrame(f);
            s->setProperties(layerId, {{"transform.position", PropValue::of(f * 8.0, f == 16 ? -60 : 0)},
                                       {"transform.rotation", PropValue::of(f * 5.0)}, {"opacity", PropValue::of(f == 24 ? 40 : 100)}});
        }
        s->setAutoKey(false);
        s->setKeySelection({{layerId, "transform.position", 8}, {layerId, "transform.rotation", 8}});
        s->setKeySelection({{layerId, "transform.position", 8}});
        s->setActiveLayer(layerId);
        tv()->setExpanded(layerId, true);
        s->setTool(ToolId::LayerTransform);
        doc->setCurrentFrame(12);
        QApplication::processEvents();
        w->grab().save(out);
    }
};

QTEST_MAIN(TestUiKeyframes)
#include "test_ui_keyframes.moc"
