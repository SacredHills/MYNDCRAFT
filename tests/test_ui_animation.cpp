#include <QtTest>
#include <QMenuBar>
#include <QPainter>

#include "TestEnv.h"
#include "Core/Settings.h"
#include "UI/Workspaces.h"
#include "UI/ExposureSheet.h"
#include "UI/MainWindow.h"
#include "UI/TimelinePanel.h"

using namespace mynd;

class TestUiAnimation : public QObject {
    Q_OBJECT
    std::unique_ptr<MainWindow> w;
    CanvasView* cv = nullptr;
    EditorSession* s = nullptr;
    Document* doc = nullptr;
    QTemporaryDir dir;
    QUuid layerId;

    QPoint view(double x, double y) { return cv->docToView().map(QPointF(x, y)).toPoint(); }
    void drag(const QList<QPointF>& pts) {
        QTest::mousePress(cv, Qt::LeftButton, {}, view(pts[0].x(), pts[0].y()));
        for (int i = 1; i < pts.size(); ++i) QTest::mouseMove(cv, view(pts[i].x(), pts[i].y()));
        QTest::mouseRelease(cv, Qt::LeftButton, {}, view(pts.last().x(), pts.last().y()));
    }
    Layer* L() { return doc->findLayer(layerId); }
    // Paint a horizontal stroke with the real brush tool at the current frame.
    void stroke(double y = 100, QColor c = Qt::black) {
        s->setForeground(c);
        s->brushFor(ToolId::Brush).size = 12; s->brushFor(ToolId::Brush).hardness = 1;
        s->setTool(ToolId::Brush);
        drag({{60, y}, {140, y}, {220, y}});
    }
    int alphaAtFrame(int frame, int x, int y) {
        const QImage* img = L()->imageAt(frame);
        return img ? qAlpha(img->pixel(x, y)) : -1;
    }
    TimelinePanel* timeline() { return w->findChild<TimelinePanel*>(); }
    ExposureSheetPanel* sheet() { return w->findChild<ExposureSheetPanel*>(); }

private slots:
    void init() {
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);   // tests must not inherit a saved layout
        w = std::make_unique<MainWindow>();
        CompositionSettings cs; cs.width = 400; cs.height = 300; cs.durationFrames = 24;
        w->newDocument(cs, false);
        w->resize(1500, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->workspaces()->apply("Animation");
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        QApplication::processEvents();
        cv = w->canvas(); s = w->session(); doc = w->document();
        layerId = s->addLayerAboveActive(LayerType::Animation, "Anim");
        cv->actualPixels();
        cv->panBy({0, 0});
        QApplication::processEvents();
    }
    void cleanup() {
        s->playback()->pause();
        w->document()->undoStack()->clear(); w->document()->markSaved(); w->close(); w.reset();
    }

    void animationWorkspaceShowsTimelineSheetAndOnionPanels() {
        QVERIFY(timeline() && sheet());
        for (const char* k : {"dock.timeline", "dock.exposure", "dock.onion"}) {
            auto* d = w->findChild<QDockWidget*>(k);
            QVERIFY2(d, k);
            QVERIFY2(!d->isHidden(), k);
        }
        w->workspaces()->apply("Drawing");
        QVERIFY(w->findChild<QDockWidget*>("dock.timeline")->isHidden());
        w->workspaces()->apply("Animation");
        QVERIFY(!w->findChild<QDockWidget*>("dock.timeline")->isHidden());
        QVERIFY(w->workspaces()->presetNames().contains("Animation"));
    }

    void paintingCreatesDrawingsPerFrameAndEachFrameShowsItsOwn() {
        QCOMPARE(L()->anim.drawings.size(), 0);
        stroke(100);                                            // frame 0
        QCOMPARE(L()->anim.drawings.size(), 1);
        QCOMPARE(L()->anim.drawings[0].name, QString("1"));
        QCOMPARE(L()->anim.at(0)->length, 1);
        QCOMPARE(alphaAtFrame(0, 140, 100), 255);
        doc->setCurrentFrame(3);
        QVERIFY(!L()->imageAt(3));                              // nothing exposed yet
        QCOMPARE(cv->compositeImage().pixelColor(140, 100), QColor(Qt::white));   // blank frame shows nothing
        stroke(180, Qt::red);
        QCOMPARE(L()->anim.drawings.size(), 2);
        QCOMPARE(L()->anim.drawingAt(3), L()->anim.drawings[1].id);
        QCOMPARE(alphaAtFrame(3, 140, 180), 255);
        QCOMPARE(alphaAtFrame(3, 140, 100), 0);                 // frame 3's drawing has only its own stroke
        QCOMPARE(alphaAtFrame(0, 140, 180), 0);
        doc->setCurrentFrame(0);
        QCOMPARE(cv->compositeImage().pixelColor(140, 100), QColor(Qt::black));
        QCOMPARE(cv->compositeImage().pixelColor(140, 180), QColor(Qt::white));
        // Undo walks back through the stroke, then the drawing creation.
        doc->setCurrentFrame(3);
        doc->undoStack()->undo();                               // stroke
        QCOMPARE(alphaAtFrame(3, 140, 180), 0);
        QCOMPARE(L()->anim.drawings.size(), 2);
        doc->undoStack()->undo();                               // New Drawing
        QCOMPARE(L()->anim.drawings.size(), 1);
        QVERIFY(!L()->imageAt(3));
    }

    void editingAHeldDrawingChangesEveryFrameThatShowsIt() {
        stroke(100);
        QVERIFY(w->animation()->extend->isEnabled());
        w->animation()->extend->trigger();
        w->animation()->extend->trigger();                      // drawing 1 now held over frames 0..2
        QCOMPARE(L()->anim.at(0)->length, 3);
        doc->setCurrentFrame(2);
        s->setForeground(Qt::blue);
        s->setTool(ToolId::Brush);
        drag({{100, 200}, {300, 200}});                         // draw while parked on the last held frame
        QCOMPARE(L()->anim.drawings.size(), 1);                 // same drawing, no new one
        doc->setCurrentFrame(0);
        QCOMPARE(alphaAtFrame(0, 200, 200), 255);               // visible on frame 0 too
        QCOMPARE(cv->compositeImage().pixelColor(200, 200), QColor(Qt::blue));
        QCOMPARE(L()->anim.drawingAt(0), L()->anim.drawingAt(2));
    }

    void timingCommandsAreUndoableAndUpdateTheTimeline() {
        s->newDrawing(false);                                   // frame 0
        doc->setCurrentFrame(1); s->newDrawing(false);
        doc->setCurrentFrame(0);
        auto* a = w->animation();
        a->extend->trigger();                                   // A A B
        QCOMPARE(L()->anim.exposures[0].length, 2);
        QCOMPARE(L()->anim.exposures[1].start, 2);
        a->shorten->trigger();
        QCOMPARE(L()->anim.exposures[1].start, 1);
        doc->undoStack()->undo();
        QCOMPARE(L()->anim.exposures[1].start, 2);
        a->insertBlank->trigger();                              // blank exposure inserted at frame 0, pushes everything
        QVERIFY(L()->anim.at(0)->drawing.isNull());
        QCOMPARE(L()->anim.exposures[1].start, 1);
        a->deleteFrame->trigger();                              // close the gap again
        QCOMPARE(L()->anim.exposures[0].start, 0);
        QVERIFY(!L()->anim.at(0)->drawing.isNull());
        // Marker toggles
        a->markKey->trigger();
        QCOMPARE(L()->anim.at(0)->marker, PoseMarker::Key);
        a->markInbetween->trigger();
        QCOMPARE(L()->anim.at(0)->marker, PoseMarker::Inbetween);
        a->markClear->trigger();
        QCOMPARE(L()->anim.at(0)->marker, PoseMarker::None);
        // Repeat exposure
        const int before = L()->anim.endFrame();
        a->duplicateExposure->trigger();
        QCOMPARE(L()->anim.endFrame(), before + 2);
        // The Animation menu exposes the commands
        bool found = false;
        for (QAction* m : w->menuBar()->actions()) if (m->text().contains("Animation")) found = true;
        QVERIFY(found);
    }

    void exposureSheetTypingExposesDrawingsAndCreatesNewOnes() {
        s->newDrawing(false);                                   // drawing "1" at frame 0
        auto* m = sheet()->model();
        const int col = m->columnOf(layerId);
        QVERIFY(col >= 0);
        QCOMPARE(m->rowCount(), 24);
        QCOMPARE(m->data(m->index(0, col), Qt::DisplayRole).toString(), QString("1"));
        QVERIFY(m->setData(m->index(4, col), "1", Qt::EditRole));            // reuse drawing 1 on frame 4
        QCOMPARE(L()->anim.drawings.size(), 1);
        QCOMPARE(L()->anim.drawingAt(4), L()->anim.drawings[0].id);
        QVERIFY(m->setData(m->index(5, col), "walk", Qt::EditRole));         // new name => new drawing
        QCOMPARE(L()->anim.drawings.size(), 2);
        QCOMPARE(L()->anim.drawings[1].name, QString("walk"));
        QVERIFY(L()->imageAt(5));
        QVERIFY(m->setData(m->index(4, col), "x", Qt::EditRole));            // explicit blank
        QVERIFY(L()->anim.at(4) && L()->anim.at(4)->drawing.isNull());
        QCOMPARE(m->data(m->index(4, col), Qt::DisplayRole).toString(), QString("×"));
        QVERIFY(m->setData(m->index(4, col), "", Qt::EditRole));             // empty clears
        QVERIFY(!L()->anim.at(4));
        // continuation rows show the hold bar
        AnimationState st = L()->anim; st.setRange(8, 3, st.drawings[0].id); doc->editAnimation(layerId, st, "hold");
        QCOMPARE(m->data(m->index(8, col), Qt::DisplayRole).toString(), QString("1"));
        QCOMPARE(m->data(m->index(9, col), Qt::DisplayRole).toString(), QString("│"));
        doc->undoStack()->undo();
        QVERIFY(!L()->anim.at(9));
        // Clicking a cell moves the playhead and selects the layer
        doc->setCurrentFrame(0);
        sheet()->table()->setCurrentIndex(m->index(7, col));
        QCOMPARE(doc->currentFrame(), 7);
        QCOMPARE(s->activeLayerId(), layerId);
        // ...and moving the playhead moves the selection
        doc->setCurrentFrame(12);
        QCOMPARE(sheet()->table()->currentIndex().row(), 12);
    }

    void substitutionAndReuseFromTheMenuModel() {
        s->newDrawing(false);                                   // "1" at 0
        doc->setCurrentFrame(1); s->newDrawing(false);          // "2" at 1
        doc->setCurrentFrame(0);
        const QUuid second = L()->anim.drawings[1].id;
        const QUuid first = L()->anim.drawings[0].id;
        w->animation()->extend->trigger();                      // "1" held over 0..1? (extends into frame 1 pushing "2")
        QCOMPARE(L()->anim.at(0)->length, 2);
        QVERIFY(s->editActiveAnimation("sub", [&](AnimationState& st) { return st.replaceDrawing(0, second); }));
        QCOMPARE(L()->anim.drawingAt(0), second);
        QCOMPARE(L()->anim.drawingAt(1), second);
        QCOMPARE(L()->anim.drawingAt(2), second);               // merged with the neighbour that was already "2"
        doc->undoStack()->undo();
        QCOMPARE(L()->anim.drawingAt(0), first);
    }

    void keyboardNavigatesFramesAndDrawings() {
        s->newDrawing(false);                                   // 0
        doc->setCurrentFrame(5); s->newDrawing(false);
        doc->setCurrentFrame(10); s->newDrawing(false);
        doc->setCurrentFrame(0);
        w->activateWindow();
        cv->setFocus();
        QTest::keyClick(w.get(), Qt::Key_Period);   QCOMPARE(doc->currentFrame(), 1);
        QTest::keyClick(w.get(), Qt::Key_Comma);    QCOMPARE(doc->currentFrame(), 0);
        QTest::keyClick(w.get(), Qt::Key_Comma);    QCOMPARE(doc->currentFrame(), 0);      // clamped
        QTest::keyClick(w.get(), Qt::Key_End);      QCOMPARE(doc->currentFrame(), 23);
        QTest::keyClick(w.get(), Qt::Key_Home);     QCOMPARE(doc->currentFrame(), 0);
        QTest::keyClick(w.get(), Qt::Key_Period, Qt::AltModifier); QCOMPARE(doc->currentFrame(), 5);
        QTest::keyClick(w.get(), Qt::Key_Period, Qt::AltModifier); QCOMPARE(doc->currentFrame(), 10);
        QTest::keyClick(w.get(), Qt::Key_Period, Qt::AltModifier); QCOMPARE(doc->currentFrame(), 10);   // no next
        QTest::keyClick(w.get(), Qt::Key_Comma, Qt::AltModifier);  QCOMPARE(doc->currentFrame(), 5);
        // straight-ahead: next frame + new drawing
        doc->setCurrentFrame(12);
        const int n = L()->anim.drawings.size();
        w->animation()->newDrawingNextFrame->trigger();
        QCOMPARE(doc->currentFrame(), 13);
        QCOMPARE(L()->anim.drawings.size(), n + 1);
        QCOMPARE(L()->anim.drawingAt(13), L()->anim.drawings.last().id);
    }

    void timelineMouseScrubsSelectsAndRetimes() {
        stroke(100);                                            // frame 0 drawing
        auto* tv = timeline()->view();
        tv->setCellWidth(20);
        QApplication::processEvents();
        const int row = tv->rowOfLayer(layerId);
        QVERIFY(row >= 0);
        // scrub on the ruler
        QTest::mouseClick(tv->viewport(), Qt::LeftButton, {}, tv->rulerPoint(7));
        QCOMPARE(doc->currentFrame(), 7);
        QTest::mousePress(tv->viewport(), Qt::LeftButton, {}, tv->rulerPoint(2));
        QTest::mouseMove(tv->viewport(), tv->rulerPoint(9));
        QTest::mouseRelease(tv->viewport(), Qt::LeftButton, {}, tv->rulerPoint(9));
        QCOMPARE(doc->currentFrame(), 9);
        // clicking a layer cell selects layer and frame
        s->setActiveLayer(QUuid());
        QTest::mouseClick(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(4, row));
        QCOMPARE(s->activeLayerId(), layerId);
        QCOMPARE(doc->currentFrame(), 4);
        // dragging the right edge of the exposure from frame 0 to 4 holds it over 5 frames
        const QRect ex = tv->exposureRect(row, 0);
        QVERIFY(!ex.isNull());
        const QPoint edge(ex.right() + 1, ex.center().y());
        QTest::mousePress(tv->viewport(), Qt::LeftButton, {}, edge);
        QTest::mouseMove(tv->viewport(), edge + QPoint(4 * 20, 0));
        QTest::mouseRelease(tv->viewport(), Qt::LeftButton, {}, edge + QPoint(4 * 20, 0));
        QCOMPARE(L()->anim.at(0)->length, 5);
        QCOMPARE(doc->undoStack()->undoText(), QString("Extend Exposure"));
        doc->undoStack()->undo();
        QCOMPARE(L()->anim.at(0)->length, 1);
        // the length / frame / fps controls
        auto* spin = timeline()->findChildren<QSpinBox*>().first();
        spin->setValue(11);
        QCOMPARE(doc->currentFrame(), 10);
    }

    void playbackAdvancesFramesAndCanvasClickStopsIt() {
        doc->setCurrentFrame(0);
        w->animation()->playPause->trigger();
        QVERIFY(s->playback()->isPlaying());
        s->playback()->tick(125000);
        QCOMPARE(doc->currentFrame(), 3);
        QTest::mouseClick(cv, Qt::LeftButton, {}, view(50, 50));         // click on canvas pauses, never draws
        QVERIFY(!s->playback()->isPlaying());
        QCOMPARE(doc->undoStack()->count(), 1);                          // only the layer creation
        w->animation()->stopPlayback->trigger();
        QCOMPARE(doc->currentFrame(), 0);
        w->animation()->loopPlayback->trigger();
        QVERIFY(!s->playback()->loop());
        doc->setFrameRate({12, 1});
        QCOMPARE(doc->comp().fps.num, 12);
    }

    void onionSkinShowsNeighboursOnCanvasButNotDuringPlayback() {
        stroke(100, Qt::black);                                          // frame 0: line at y=100
        doc->setCurrentFrame(1);
        stroke(200, Qt::black);                                          // frame 1: line at y=200
        Compositor::OnionSkin o = s->onion();
        o.enabled = true; o.before = 1; o.after = 0; o.opacity = 1.0; o.activeLayerOnly = true;
        s->setOnion(o);
        QApplication::processEvents();
        QColor ghost = cv->compositeImage().pixelColor(140, 100);
        QVERIFY2(ghost.red() > 200 && ghost.green() < 120, qPrintable(ghost.name()));   // previous drawing tinted red
        QCOMPARE(cv->compositeImage().pixelColor(140, 200), QColor(Qt::black));         // current drawing untinted
        s->playback()->play();
        QApplication::processEvents();
        QCOMPARE(cv->compositeImage().pixelColor(140, 100), QColor(Qt::white));          // hidden while playing
        s->playback()->pause();
        QApplication::processEvents();
        QVERIFY(cv->compositeImage().pixelColor(140, 100).red() > 200);
        // an export never contains onion skin
        const QString out = dir.filePath("frame.png");
        QString err;
        QVERIFY2(w->exportImage(out, &err), qPrintable(err));
        QCOMPARE(QImage(out).pixelColor(140, 100), QColor(Qt::white));
        o.enabled = false; s->setOnion(o);
        QApplication::processEvents();
        QCOMPARE(cv->compositeImage().pixelColor(140, 100), QColor(Qt::white));
    }

    void transformAndBucketWorkOnTheDrawingAtTheCurrentFrame() {
        stroke(100);                                                     // frame 0
        doc->setCurrentFrame(2);
        s->setTool(ToolId::Bucket);
        QTest::mouseClick(cv, Qt::LeftButton, {}, view(10, 10));  // no drawing here: refuses, does not crash
        QVERIFY(!L()->imageAt(2));
        doc->setCurrentFrame(0);
        s->setTool(ToolId::FreeTransform);
        drag({{100, 100}, {100, 160}});                                  // drag inside the content bounds (away from handles) = move down 60
        QCOMPARE(alphaAtFrame(0, 140, 160), 255);
        QCOMPARE(alphaAtFrame(0, 140, 100), 0);
        doc->setCurrentFrame(5);
        doc->undoStack()->undo();                                        // undo targets the drawing, wherever the playhead is
        QCOMPARE(alphaAtFrame(0, 140, 100), 255);
    }

    void animationSurvivesSaveAndReopen() {
        stroke(100, Qt::red);
        doc->setCurrentFrame(2); stroke(150, Qt::blue);
        w->animation()->extend->trigger();
        w->animation()->markKey->trigger();
        doc->setDuration(40);
        doc->setFrameRate({25, 1});
        const QString path = dir.filePath("anim.studio");
        QString err;
        QVERIFY2(w->saveFile(path, &err), qPrintable(err));
        QVERIFY2(w->openFile(path, &err), qPrintable(err));
        Document* d2 = w->document();
        QVERIFY(d2 != doc);
        Layer* l = nullptr;
        Document::forEachLayer(d2->root(), [&](Layer* x) { if (x->type == LayerType::Animation) l = x; });
        QVERIFY(l);
        QCOMPARE(l->anim.drawings.size(), 2);
        QCOMPARE(l->anim.at(2)->length, 2);
        QCOMPARE(l->anim.at(2)->marker, PoseMarker::Key);
        QCOMPARE(d2->durationFrames(), 40);
        QCOMPARE(d2->comp().fps.num, 25);
        QCOMPARE(QColor(l->imageAt(0)->pixel(140, 100)), QColor(Qt::red));
        QCOMPARE(QColor(l->imageAt(2)->pixel(140, 150)), QColor(Qt::blue));
        // the exposure sheet and timeline follow the new document
        QCOMPARE(sheet()->model()->rowCount(), 40);
        QVERIFY(timeline()->view()->rowOfLayer(l->id) >= 0);
        layerId = l->id; doc = d2;
        QCOMPARE(cv->compositeImage().pixelColor(140, 100), QColor(Qt::red));
    }
    // Layer content used by the copy/paste tests: frame 0 = red drawing, frames 1-2 = blue drawing (held), frame 3 empty.
    void makeClipSource() {
        doc->setCurrentFrame(0);  const QUuid a = s->newDrawing(false);
        L()->drawingImages[a].fill(Qt::red);
        doc->setCurrentFrame(1);  const QUuid b = s->newDrawing(false);
        L()->drawingImages[b].fill(Qt::blue);
        doc->setCurrentFrame(1);
        w->animation()->extend->trigger();                               // blue held over frames 1..2
        doc->undoStack()->clear();
    }

    void draggingAcrossTimelineCellsSelectsFramesAndShiftClickExtends() {
        makeClipSource();
        auto* tv = timeline()->view();
        tv->setCellWidth(20);
        QApplication::processEvents();
        const int row = tv->rowOfLayer(layerId);
        QTest::mousePress(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(1, row));
        QTest::mouseMove(tv->viewport(), tv->cellCenter(3, row));
        QTest::mouseRelease(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(3, row));
        int a, b; s->selectedFrameRange(&a, &b);
        QVERIFY(s->hasFrameSelection());
        QCOMPARE(a, 1); QCOMPARE(b, 3);
        QCOMPARE(doc->currentFrame(), 3);                                // playhead follows the drag end
        // dragging backwards works too
        QTest::mousePress(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(6, row));
        QTest::mouseMove(tv->viewport(), tv->cellCenter(4, row));
        QTest::mouseRelease(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(4, row));
        s->selectedFrameRange(&a, &b);
        QCOMPARE(a, 4); QCOMPARE(b, 6);
        // Shift+click extends from the end that is not under the playhead
        QTest::mouseClick(tv->viewport(), Qt::LeftButton, Qt::ShiftModifier, tv->cellCenter(9, row));
        s->selectedFrameRange(&a, &b);
        QCOMPARE(a, 6); QCOMPARE(b, 9);
        // a plain click selects just that frame
        QTest::mouseClick(tv->viewport(), Qt::LeftButton, {}, tv->cellCenter(2, row));
        s->selectedFrameRange(&a, &b);
        QCOMPARE(a, 2); QCOMPARE(b, 2);
        // moving the playhead any other way clears the selection
        s->setFrameSelection(layerId, 1, 4);
        QVERIFY(s->hasFrameSelection());
        QTest::keyClick(w.get(), Qt::Key_Period);
        QVERIFY(!s->hasFrameSelection());
        // so does activating another layer
        s->setFrameSelection(layerId, 1, 4);
        s->setActiveLayer(QUuid());
        QVERIFY(!s->hasFrameSelection());
        // the selection is clamped to the composition
        s->setActiveLayer(layerId);
        s->setFrameSelection(layerId, -5, 999);
        s->selectedFrameRange(&a, &b);
        QCOMPARE(a, 0); QCOMPARE(b, 23);
    }

    void pasteMakesIndependentCopiesAtThePlayheadAndOverwrites() {
        makeClipSource();
        s->setFrameSelection(layerId, 0, 3);
        QVERIFY(s->copyFrames(false));
        QCOMPARE(s->frameClip().length, 4);
        QCOMPARE(s->frameClip().exposures.size(), 2);                    // red@0, blue@1+2; frame 3 is a gap
        doc->setCurrentFrame(10);
        const QUuid red = L()->anim.drawings[0].id, blue = L()->anim.drawings[1].id;
        QVERIFY(s->pasteFrames(false));
        QCOMPARE(L()->anim.drawings.size(), 4);                          // two new drawings
        QCOMPARE(L()->anim.at(10)->length, 1);
        QCOMPARE(L()->anim.at(11)->length, 2);
        QVERIFY(L()->anim.drawingAt(10) != red);                         // independent, not the original
        QVERIFY(L()->anim.drawingAt(11) != blue);
        QVERIFY(!L()->anim.at(13));                                      // the gap came along
        QCOMPARE(doc->currentFrame(), 10);                               // playhead stays where it was pasted
        int a, b; s->selectedFrameRange(&a, &b);
        QCOMPARE(a, 10); QCOMPARE(b, 13);                                // pasted range is selected
        QCOMPARE(QColor(L()->imageAt(10)->pixel(5, 5)), QColor(Qt::red));
        QCOMPARE(QColor(L()->imageAt(12)->pixel(5, 5)), QColor(Qt::blue));
        { QPainter pp(&L()->drawingImages[L()->anim.drawingAt(10)]); pp.fillRect(0, 0, 20, 20, Qt::green); }    // editing the copy...
        QCOMPARE(QColor(L()->imageAt(0)->pixel(5, 5)), QColor(Qt::red)); // ...leaves the original alone
        { QPainter pp(&L()->drawingImages[red]); pp.fillRect(0, 0, 20, 20, Qt::yellow); }    // and the clip is a snapshot too
        QCOMPARE(QColor(s->frameClip().images.value(red).pixel(5, 5)), QColor(Qt::red));
        // one undo removes the whole paste
        doc->undoStack()->undo();
        QVERIFY(!L()->anim.at(10) && !L()->anim.at(11));
        QCOMPARE(L()->anim.drawings.size(), 2);
        doc->undoStack()->redo();
        QCOMPARE(L()->anim.drawings.size(), 4);
        QVERIFY(L()->imageAt(11));                                       // redo brought the pixels back
        // pasting over existing frames overwrites them (including the gap)
        doc->setCurrentFrame(2);
        QVERIFY(s->pasteFrames(false));
        QCOMPARE(L()->anim.at(2)->start, 2);
        QCOMPARE(L()->anim.at(3)->length, 2);
        QVERIFY(!L()->anim.at(5));
    }

    void linkedPasteReusesDrawingsSoEditsAreShared() {
        makeClipSource();
        s->setFrameSelection(layerId, 0, 2);
        s->copyFrames(false);
        doc->setCurrentFrame(8);
        QVERIFY(s->pasteFrames(true));
        QCOMPARE(L()->anim.drawings.size(), 2);                          // no new drawings
        QCOMPARE(L()->anim.drawingAt(8), L()->anim.drawingAt(0));
        QCOMPARE(L()->anim.drawingAt(10), L()->anim.drawingAt(2));
        s->setTool(ToolId::Brush);
        doc->setCurrentFrame(8);
        s->setForeground(Qt::black);
        s->brushFor(ToolId::Brush).size = 12;
        drag({{50, 50}, {150, 50}});
        QCOMPARE(QColor(L()->imageAt(0)->pixel(100, 50)), QColor(Qt::black));   // the original changed too
        // Pasting linked onto a different layer cannot share, so it falls back to independent copies.
        const QUuid other = s->addLayerAboveActive(LayerType::Animation, "Other");
        doc->setCurrentFrame(4);
        QVERIFY(s->pasteFrames(true));
        const Layer* ol = doc->findLayer(other);
        QCOMPARE(ol->anim.drawings.size(), 2);
        QVERIFY(ol->imageAt(4));
        QCOMPARE(ol->anim.at(5)->length, 2);
    }

    void cutRemovesFramesButKeepsThemForPasting() {
        makeClipSource();
        s->setFrameSelection(layerId, 1, 2);
        QVERIFY(s->copyFrames(true));
        QVERIFY(!L()->anim.at(1) && !L()->anim.at(2));                   // cut leaves empty frames, nothing shifts
        QVERIFY(L()->anim.at(0));
        QVERIFY(!s->hasFrameSelection());
        doc->setCurrentFrame(6);
        QVERIFY(s->pasteFrames(false));
        QCOMPARE(L()->anim.at(6)->length, 2);
        QCOMPARE(QColor(L()->imageAt(7)->pixel(3, 3)), QColor(Qt::blue));
        doc->undoStack()->undo();                                        // paste
        doc->undoStack()->undo();                                        // cut
        QVERIFY(L()->anim.at(1) && L()->anim.at(2));
        // cut on a locked layer neither cuts nor changes the clip
        LayerProps p = L()->props; p.locked = true; doc->setLayerProps(layerId, p);
        const int len = s->frameClip().length;
        s->setFrameSelection(layerId, 0, 0);
        QVERIFY(!s->copyFrames(true));
        QVERIFY(L()->anim.at(0));
        QCOMPARE(s->frameClip().length, len);
        QVERIFY(!s->pasteFrames(false));
    }

    void pastePastTheEndGrowsTheCompositionAndKeyboardShortcutsWork() {
        makeClipSource();
        w->activateWindow(); cv->setFocus();
        s->setFrameSelection(layerId, 0, 3);
        QTest::keyClick(w.get(), Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(s->frameClip().length, 4);
        doc->setCurrentFrame(22);                                        // composition is 24 frames
        QTest::keyClick(w.get(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(doc->durationFrames(), 25);                             // grew to fit the last pasted exposure (the trailing gap needs no frames)
        QVERIFY(L()->anim.at(22));
        QCOMPARE(doc->undoStack()->undoText(), QString("Paste Frames"));
        doc->undoStack()->undo();
        QCOMPARE(doc->durationFrames(), 24);                             // duration growth is part of the same undo step
        // Ctrl+Shift+V = linked
        doc->setCurrentFrame(12);
        const int n = L()->anim.drawings.size();
        QTest::keyClick(w.get(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(L()->anim.drawings.size(), n);
        // Ctrl+X
        s->setFrameSelection(layerId, 12, 12);
        QTest::keyClick(w.get(), Qt::Key_X, Qt::ControlModifier);
        QVERIFY(!L()->anim.at(12));
    }

    void contextMenuOffersCopyCutPasteAndTracksTheClipboardState() {
        makeClipSource();
        QMenu m;
        w->animation()->fillContextMenu(&m);
        QStringList texts; for (QAction* a : m.actions()) texts << a->text();
        QVERIFY(texts.contains("Copy Frames") && texts.contains("Cut Frames") && texts.contains("Paste Frames"));
        QVERIFY(texts.contains("Paste Frames (Reuse Same Drawings)"));
        QVERIFY(w->animation()->copyFrames->isEnabled());
        QVERIFY(!w->animation()->pasteFrames->isEnabled());               // nothing copied yet
        s->setFrameSelection(layerId, 0, 1);
        w->animation()->copyFrames->trigger();
        QVERIFY(w->animation()->pasteFrames->isEnabled());
        s->setActiveLayer(QUuid());                                       // no animation layer active
        QVERIFY(!w->animation()->pasteFrames->isEnabled());
        QVERIFY(!w->animation()->copyFrames->isEnabled());
    }

    void deleteFramesActsOnTheWholeSelectionAndClosesTheGap() {
        makeClipSource();                                                 // red@0, blue@1..2
        doc->setCurrentFrame(5); const QUuid c = s->newDrawing(false);
        L()->drawingImages[c].fill(Qt::green);
        s->setFrameSelection(layerId, 1, 4);                              // blue + the empty frames after it
        w->animation()->deleteFrame->trigger();
        QCOMPARE(L()->anim.at(1)->drawing, c);                            // green slid left over the gap
        QCOMPARE(L()->anim.at(0)->length, 1);
        QCOMPARE(doc->undoStack()->undoText(), QString("Delete Frames"));
    }

    // Optional: MYND_SHOT=/path/out.png renders the Animation workspace with sample content for visual review.
    void zzScreenshotForReview() {
        const QString out = qEnvironmentVariable("MYND_SHOT");
        if (out.isEmpty()) QSKIP("set MYND_SHOT to write a screenshot");
        s->brushFor(ToolId::Brush).size = 10;
        for (int f = 0; f < 6; ++f) {
            doc->setCurrentFrame(f * 2);
            s->setForeground(QColor(30, 30, 30)); s->setTool(ToolId::Brush);
            drag({{80.0 + f * 40, 220}, {110.0 + f * 40, 120.0 + (f % 3) * 20}, {140.0 + f * 40, 220}});
        }
        doc->setCurrentFrame(4);
        w->animation()->extend->trigger(); w->animation()->markKey->trigger();
        doc->setCurrentFrame(8); w->animation()->markBreakdown->trigger();
        doc->setCurrentFrame(10); w->animation()->markInbetween->trigger();
        doc->setCurrentFrame(14); w->animation()->insertBlank->trigger();
        s->addLayerAboveActive(LayerType::Raster, "Background");
        s->setActiveLayer(layerId);
        Compositor::OnionSkin o = s->onion(); o.enabled = true; o.before = 2; o.after = 1; s->setOnion(o);
        doc->setCurrentFrame(6);
        QApplication::processEvents();
        QVERIFY(w->grab().save(out));
    }
};

QTEST_MAIN(TestUiAnimation)
#include "test_ui_animation.moc"
