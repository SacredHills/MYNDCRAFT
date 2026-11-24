#include <QtTest>

#include "TestEnv.h"
#include "Scene/Document.h"

using namespace mynd;

class TestDocument : public QObject {
    Q_OBJECT
    static std::unique_ptr<Document> make() {
        CompositionSettings s; s.width = 64; s.height = 48;
        return std::make_unique<Document>(s);
    }
    static QStringList names(const Layer* g) {
        QStringList n;
        for (auto& c : g->children) { n << c->props.name; if (c->isGroup()) n << "[" + names(c.get()).join(",") + "]"; }
        return n;
    }
private slots:
    void addRemoveUndo() {
        auto d = make();
        const QUuid a = d->addLayer(LayerType::Raster, "A");
        const QUuid b = d->addLayer(LayerType::Vector, "B");
        QCOMPARE(names(d->root()), (QStringList{"A", "B"}));
        QVERIFY(d->findLayer(a)->raster.size() == QSize(64, 48));
        d->removeLayer(a);
        QCOMPARE(names(d->root()), (QStringList{"B"}));
        d->undoStack()->undo();
        QCOMPARE(names(d->root()), (QStringList{"A", "B"}));
        QCOMPARE(d->root()->children[0]->id, a);   // identity survives undo
        d->undoStack()->undo();
        d->undoStack()->undo();
        QCOMPARE(d->layerCount(), 0);
        d->undoStack()->redo(); d->undoStack()->redo();
        QCOMPARE(d->root()->children[1]->id, b);
    }
    void groupUngroup() {
        auto d = make();
        d->addLayer(LayerType::Raster, "A");
        const QUuid b = d->addLayer(LayerType::Raster, "B");
        d->addLayer(LayerType::Raster, "C");
        const QUuid g = d->groupLayer(b);
        QCOMPARE(names(d->root()), (QStringList{"A", "B", "[B]", "C"}.mid(0, 0) + QStringList{"A", "Group", "[B]", "C"}));
        QCOMPARE(d->findLayer(b)->parent, d->findLayer(g));
        d->undoStack()->undo();   // single undo step for the whole grouping
        QCOMPARE(names(d->root()), (QStringList{"A", "B", "C"}));
        d->undoStack()->redo();
        d->ungroupLayer(g);
        QCOMPARE(names(d->root()), (QStringList{"A", "B", "C"}));
        d->undoStack()->undo();
        QCOMPARE(names(d->root()), (QStringList{"A", "Group", "[B]", "C"}));
    }
    void moveIntoGroupAndCycleGuard() {
        auto d = make();
        const QUuid a = d->addLayer(LayerType::Raster, "A");
        const QUuid g = d->addLayer(LayerType::Group, "G");
        const QUuid h = d->addLayer(LayerType::Group, "H", g);
        d->moveLayer(a, g, 0);
        QCOMPARE(d->findLayer(a)->parent, d->findLayer(g));
        QCOMPARE(d->indexInParent(d->findLayer(a)), 0);
        d->moveLayer(g, h, 0);  // a group cannot move into its own descendant
        QCOMPARE(d->findLayer(g)->parent, d->root());
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(a)->parent, d->root());
    }
    void duplicateGetsFreshIds() {
        auto d = make();
        const QUuid v = d->addLayer(LayerType::Vector, "V");
        ShapeList s{VectorShape::makeRect(QRectF(1, 2, 10, 10))};
        d->setShapes(v, s, "x");
        const QUuid c = d->duplicateLayer(v);
        QVERIFY(c != v);
        QCOMPARE(d->findLayer(c)->shapes.size(), 1);
        QVERIFY(d->findLayer(c)->shapes[0].id != s[0].id);
        QCOMPARE(d->findLayer(c)->shapes[0].nodes, s[0].nodes);
        QCOMPARE(d->indexInParent(d->findLayer(c)), 1);
    }
    void propertyChangesMergeWhenDragged() {
        auto d = make();
        const QUuid a = d->addLayer(LayerType::Raster, "A");
        const int base = d->undoStack()->count();
        for (double o : {0.9, 0.7, 0.5}) { LayerProps p = d->findLayer(a)->props; p.opacity = o; d->setLayerProps(a, p, "opacity"); }
        QCOMPARE(d->undoStack()->count(), base + 1);   // slider drag = one undo step
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(a)->props.opacity, 1.0);
        LayerProps p = d->findLayer(a)->props; p.name = "Renamed"; d->setLayerProps(a, p);
        QCOMPARE(d->findLayer(a)->props.name, QString("Renamed"));
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(a)->props.name, QString("A"));
    }
    void modifiedFlagTracksUndoStack() {
        auto d = make();
        QVERIFY(!d->isModified());
        d->addLayer(LayerType::Raster, "A");
        QVERIFY(d->isModified());
        d->markSaved();
        QVERIFY(!d->isModified());
        d->undoStack()->undo();
        QVERIFY(d->isModified());
        d->undoStack()->redo();
        QVERIFY(!d->isModified());
    }
    void frameRateIsExactWithoutDrift() {
        FrameRate ntsc{24000, 1001};
        QCOMPARE(ntsc.frameToMicros(24000), 1001000000LL);   // exactly 1001 s
        QCOMPARE(ntsc.frameToMicros(0), 0LL);
        FrameRate f24{24, 1};
        QCOMPARE(f24.frameToMicros(24 * 3600 * 10), 36000000000LL);
        QVERIFY(!(FrameRate{0, 1}.isValid()));
    }
    void canvasSizeIsClamped() {
        CompositionSettings s; s.width = 999999; s.height = -4;
        Document d(s);
        QCOMPARE(d.comp().width, kMaxCanvasDim);
        QCOMPARE(d.comp().height, 1);
    }

    void textLayerEditsMergeAndUndo() {
        auto d = make();
        TextContent c; c.text = "Hello"; c.pos = {5, 5}; c.sizePx = 20; c.color = Qt::black;
        const QUuid id = d->addTextLayer(c, "Hello");
        QCOMPARE(d->findLayer(id)->type, LayerType::Text);
        const int base = d->undoStack()->count();
        TextContent a = c; a.sizePx = 24;  d->setTextContent(id, a, "Change Text Style", "text-style");
        TextContent b = c; b.sizePx = 30;  d->setTextContent(id, b, "Change Text Style", "text-style");
        QCOMPARE(d->undoStack()->count(), base + 1);                   // merged
        QCOMPARE(d->findLayer(id)->textContent.sizePx, 30.0);
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(id)->textContent.sizePx, 20.0);
        d->undoStack()->redo();
        QCOMPARE(d->findLayer(id)->textContent.sizePx, 30.0);
        TextContent e = b; e.text = "Other";
        d->setTextContent(id, e, "Edit Text");                         // no merge key => own step
        QCOMPARE(d->undoStack()->count(), base + 2);
    }
    void importedImageIsCenteredAndScaledToFit() {
        auto d = make();                                              // 64x48 canvas
        QImage small(16, 8, QImage::Format_ARGB32); small.fill(Qt::red);
        const QUuid a = d->addImageLayer(small, "small");
        const Layer* la = d->findLayer(a);
        QCOMPARE(qAlpha(la->raster.pixel(32, 24)), 255);              // centered, not scaled
        QCOMPARE(qAlpha(la->raster.pixel(32 - 9, 24)), 0);
        QImage big(256, 96, QImage::Format_ARGB32); big.fill(Qt::blue);
        const QUuid b = d->addImageLayer(big, "big");
        const Layer* lb = d->findLayer(b);
        QCOMPARE(qAlpha(lb->raster.pixel(1, 24)), 255);               // scaled by 4 to 64x24 -> spans full width
        QCOMPARE(qAlpha(lb->raster.pixel(32, 0)), 0);
        QCOMPARE(qAlpha(lb->raster.pixel(32, 24)), 255);
        d->undoStack()->undo();
        QVERIFY(!d->findLayer(b));
        QCOMPARE(d->undoStack()->undoText(), QString("Import Image"));
    }

    void animationLayerEditsAreUndoableAndGrowTheDuration() {
        auto d = make();                                               // duration 48
        const QUuid lid = d->addAnimationLayer("anim");
        Layer* l = d->findLayer(lid);
        QCOMPARE(l->type, LayerType::Animation);
        const QUuid a = d->createDrawing(lid, 0, false);
        QCOMPARE(l->anim.drawingAt(0), a);
        QCOMPARE(l->anim.drawings.size(), 1);
        QCOMPARE(l->anim.drawings[0].name, QString("1"));
        QVERIFY(l->imageForEdit(0));
        QVERIFY(!l->imageForEdit(1));
        const int base = d->undoStack()->count();
        AnimationState s = l->anim; s.setRange(0, 60, a);              // hold past the end of the composition
        d->editAnimation(lid, s, "Hold");
        QCOMPARE(d->durationFrames(), 60);
        QCOMPARE(d->undoStack()->count(), base + 1);                   // duration + timing = one undo step
        d->undoStack()->undo();
        QCOMPARE(d->durationFrames(), 48);
        QCOMPARE(l->anim.endFrame(), 1);
        d->undoStack()->redo();
        QCOMPARE(d->durationFrames(), 60);
        QCOMPARE(l->anim.endFrame(), 60);
    }
    void drawingPixelsAreSharedByEveryExposureAndEditsUndoPerDrawing() {
        auto d = make();
        const QUuid lid = d->addAnimationLayer("anim");
        Layer* l = d->findLayer(lid);
        const QUuid a = d->createDrawing(lid, 0, false);
        AnimationState s = l->anim; s.setRange(0, 2, a); s.setRange(4, 2, a); d->editAnimation(lid, s, "Reuse");
        d->setCurrentFrame(4);
        QImage* img = d->editImage(l);
        QVERIFY(img);
        QImage before = img->copy();
        QPainter p(img); p.fillRect(3, 3, 5, 5, Qt::red); p.end();
        d->commitRasterPatch(lid, QRect(0, 0, 10, 10), before, "stroke");
        d->setCurrentFrame(1);                                         // the other exposure of the same drawing
        QCOMPARE(qAlpha(d->editImage(l)->pixel(4, 4)), 255);
        d->setCurrentFrame(10);                                        // elsewhere: undo must still hit the drawing, not the frame
        d->undoStack()->undo();
        d->setCurrentFrame(0);
        QCOMPARE(qAlpha(d->editImage(l)->pixel(4, 4)), 0);
        d->undoStack()->redo();
        QCOMPARE(qAlpha(d->editImage(l)->pixel(4, 4)), 255);
    }
    void duplicateDrawingCopiesPixelsAndSplitsAHold() {
        auto d = make();
        const QUuid lid = d->addAnimationLayer("anim");
        Layer* l = d->findLayer(lid);
        const QUuid a = d->createDrawing(lid, 0, false);
        l->drawingImages[a].fill(Qt::green);
        AnimationState s = l->anim; s.setRange(0, 5, a); d->editAnimation(lid, s, "Hold");
        const QUuid b = d->createDrawing(lid, 2, true);                // substitute frame 2 with a copy
        QCOMPARE(l->anim.drawingAt(1), a);
        QCOMPARE(l->anim.drawingAt(2), b);
        QCOMPARE(l->anim.drawingAt(3), a);
        QCOMPARE(l->anim.drawings[1].name, QString("2"));
        QCOMPARE(QColor(l->drawingImages[b].pixel(1, 1)), QColor(Qt::green));
        l->drawingImages[b].fill(Qt::blue);
        QCOMPARE(QColor(l->drawingImages[a].pixel(1, 1)), QColor(Qt::green));   // independent pixels
        d->undoStack()->undo();
        QCOMPARE(l->anim.drawingAt(2), a);
        QCOMPARE(l->anim.drawings.size(), 1);
    }
    void timingEditsAreUndoableAndClampThePlayhead() {
        auto d = make();
        d->setDuration(100);
        d->setCurrentFrame(80);
        QCOMPARE(d->currentFrame(), 80);
        d->setWorkArea(true, 10, 20);
        QCOMPARE(d->rangeStart(), 10); QCOMPARE(d->rangeEnd(), 20);
        d->setFrameRate({30000, 1001});
        QVERIFY(!d->comp().fps.isValid() == false);
        d->setFrameRate({0, 1});                                       // rejected
        QCOMPARE(d->comp().fps.num, 30000);
        d->setDuration(30);
        QCOMPARE(d->currentFrame(), 29);                               // playhead pulled back inside
        QCOMPARE(d->rangeEnd(), 20);
        d->undoStack()->undo();
        QCOMPARE(d->durationFrames(), 100);
        d->setCurrentFrame(1000);
        QCOMPARE(d->currentFrame(), 99);
        d->setCurrentFrame(-5);
        QCOMPARE(d->currentFrame(), 0);
    }
};

QTEST_MAIN(TestDocument)
#include "test_document.moc"
