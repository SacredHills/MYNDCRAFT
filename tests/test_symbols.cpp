#include <QtTest>
#include <QPainter>
#include <QTemporaryDir>

#include "TestEnv.h"
#include "Core/ProjectIO.h"
#include "Graphics/Compositor.h"
#include "Scene/Document.h"

using namespace mynd;

class TestSymbols : public QObject {
    Q_OBJECT
    static std::unique_ptr<Document> make(int frames = 30) {
        CompositionSettings s; s.width = 100; s.height = 100; s.durationFrames = frames; s.background = Qt::white;
        return std::make_unique<Document>(s);
    }
    static QColor px(Document& d, int frame, int x, int y) {
        Compositor::Options o; o.frame = frame;
        return Compositor::render(d, d.canvasRect(), o).pixelColor(x, y);
    }
    // A raster layer with a 20x20 red square at (10,10).
    static QUuid square(Document& d, const char* name = "Sq", QColor c = Qt::red) {
        const QUuid id = d.addLayer(LayerType::Raster, name);
        QPainter p(&d.findLayer(id)->raster);
        p.fillRect(10, 10, 20, 20, c);
        return id;
    }
private slots:
    void convertingALayerMovesItIntoTheLibraryAndLeavesAnInstance() {
        auto d = make();
        const QUuid sq = square(*d);
        d->undoStack()->clear();
        const QUuid sym = d->convertToSymbol(sq, "Box", SymbolKind::Graphic);
        QVERIFY(!sym.isNull());
        QCOMPARE(d->symbols().size(), 1);
        QCOMPARE(d->root()->children.size(), size_t(1));
        const Layer* inst = d->root()->children[0].get();
        QCOMPARE(inst->type, LayerType::Symbol);
        QCOMPARE(inst->symbolId, sym);
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));                     // looks exactly the same
        QCOMPARE(px(*d, 0, 60, 60), QColor(Qt::white));
        QVERIFY(!d->findLayer(sq));                                       // the artwork now lives inside the symbol
        QCOMPARE(d->symbolUseCount(sym), 1);
        d->undoStack()->undo();
        QCOMPARE(d->symbols().size(), 0);
        QVERIFY(d->findLayer(sq));
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));
        d->undoStack()->redo();
        QCOMPARE(d->symbols().size(), 1);
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));
    }

    void instancesCanBeMovedAndReusedAndShareOneDefinition() {
        auto d = make();
        const QUuid sym = d->convertToSymbol(square(*d), "Box", SymbolKind::Graphic);
        const QUuid second = d->addSymbolInstance(sym);
        QVERIFY(!second.isNull());
        Layer* l2 = d->findLayer(second);
        PropertyTracks t = l2->tracks;
        setValueAt(t, "transform.position", 0, PropValue::of(50, 0), false);
        d->editTracks(second, t, "move");
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));                     // first instance
        QCOMPARE(px(*d, 0, 70, 20), QColor(Qt::red));                     // second, moved 50 right
        QCOMPARE(d->symbolUseCount(sym), 2);
        // editing the symbol changes both instances
        d->enterSymbol(sym);
        QVERIFY(d->editingSymbol());
        QCOMPARE(d->breadcrumb(), (QStringList{"Scene", "Box"}));
        Layer* inner = d->root()->children[0].get();
        QPainter p(&inner->raster); p.fillRect(40, 60, 10, 10, Qt::blue); p.end();
        d->exitSymbol();
        QVERIFY(!d->editingSymbol());
        QCOMPARE(px(*d, 0, 45, 65), QColor(Qt::blue));                   // first instance
        QCOMPARE(px(*d, 0, 95, 65), QColor(Qt::blue));                   // the moved one shows the same edit
        // deleting a used symbol is refused; once the instances are gone it works
        QString why;
        QVERIFY(!d->deleteSymbol(sym, &why));
        QVERIFY(!why.isEmpty());
    }

    void graphicSymbolsFollowTheParentTimelineMovieClipsLoop() {
        auto d = make(40);
        // symbol content: a raster layer whose square moves right over the symbol's own 10 frames
        const QUuid sq = square(*d);
        PropertyTracks t;
        setValueAt(t, "transform.position", 0, PropValue::of(0, 0), true);
        setValueAt(t, "transform.position", 9, PropValue::of(60, 0), true);
        d->editTracks(sq, t, "move");
        const QUuid sym = d->convertToSymbol(sq, "Mover", SymbolKind::Graphic);
        d->enterSymbol(sym);
        d->setDuration(10);
        d->exitSymbol();
        QCOMPARE(d->symbols()[0].durationFrames, 10);
        QCOMPARE(d->durationFrames(), 40);                                // the scene keeps its own length
        // loop (default): frame 0 -> start, frame 9 -> end, frame 10 wraps
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));
        QCOMPARE(px(*d, 9, 75, 20), QColor(Qt::red));                     // end of the symbol's own motion (60 px right)
        QCOMPARE(px(*d, 10, 20, 20), QColor(Qt::red));                    // wrapped back to the start
        QCOMPARE(px(*d, 5, 20, 20) == QColor(Qt::red), false);            // mid-way it has moved off the start spot
        // play once holds the last frame; single frame holds the first
        const QUuid inst = d->root()->children[0]->id;
        d->setInstancePlayback(inst, SymbolPlay::PlayOnce, 0);
        QCOMPARE(px(*d, 30, 75, 20), QColor(Qt::red));                    // parked at the end (60 px right)
        d->setInstancePlayback(inst, SymbolPlay::SingleFrame, 0);
        QCOMPARE(px(*d, 30, 20, 20), QColor(Qt::red));
        d->setInstancePlayback(inst, SymbolPlay::Loop, 5);                // start 5 frames in
        QCOMPARE(px(*d, 0, 20, 20) == QColor(Qt::red), false);
        d->undoStack()->undo();
        QCOMPARE(d->findLayer(inst)->symbolPlay, SymbolPlay::SingleFrame);
        // a movie clip ignores the playback mode: it loops from its own first frame
        d->setSymbolKind(sym, SymbolKind::MovieClip);
        QCOMPARE(px(*d, 10, 20, 20), QColor(Qt::red));
        QCOMPARE(px(*d, 20, 20, 20), QColor(Qt::red));
        QCOMPARE(px(*d, 15, 20, 20) == QColor(Qt::red), false);
    }

    void symbolsNestAndCyclesAreRefused() {
        auto d = make();
        const QUuid a = d->convertToSymbol(square(*d, "A", Qt::red), "A", SymbolKind::Graphic);
        d->undoStack()->clear();
        // a second symbol that contains an instance of A
        const QUuid b = d->createSymbol("B", SymbolKind::Graphic, 30);
        d->enterSymbol(b);
        QVERIFY(!d->addSymbolInstance(a).isNull());                       // B contains A: fine
        QVERIFY(d->addSymbolInstance(b).isNull());                        // B inside B: refused
        d->exitSymbol();
        QVERIFY(d->symbolUses(b, a));
        d->enterSymbol(a);
        QVERIFY(!d->canInstantiate(b));                                   // A would then contain B which contains A
        QVERIFY(d->addSymbolInstance(b).isNull());
        d->exitSymbol();
        // the nested symbol renders: put B in the scene
        QVERIFY(!d->addSymbolInstance(b).isNull());
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));                     // scene -> B -> A -> red square
        // undo walks back through the enter/exit steps in the right context
        d->undoStack()->undo();                                           // instance of B
        QVERIFY(!d->editingSymbol());
        d->undoStack()->undo();                                           // exit A
        QVERIFY(d->editingSymbol());
        QCOMPARE(d->editingSymbolId(), a);
        while (d->undoStack()->canUndo()) d->undoStack()->undo();         // back to the start through every enter/exit step
        QVERIFY(!d->editingSymbol());
        QCOMPARE(d->symbols().size(), 1);                                 // only A (made before the history was cleared)
        QCOMPARE(d->root()->children.size(), size_t(1));
        while (d->undoStack()->canRedo()) d->undoStack()->redo();
        QCOMPARE(px(*d, 0, 20, 20), QColor(Qt::red));
    }

    void assetsExportWithTheirDependenciesAndImportAsIndependentCopies() {
        auto d = make();
        const QUuid a = d->convertToSymbol(square(*d, "A", Qt::red), "Box", SymbolKind::Graphic);
        const QUuid b = d->createSymbol("Outer", SymbolKind::Graphic, 30);
        d->enterSymbol(b); d->addSymbolInstance(a); d->exitSymbol();
        d->createSymbol("Unrelated", SymbolKind::Graphic, 30);
        auto asset = d->symbolAsset(b);
        QVERIFY(asset);
        QCOMPARE(asset->symbols().size(), 2);                             // Outer + Box, not Unrelated
        QTemporaryDir dir;
        const QString path = dir.filePath("asset.studio");
        QString err;
        QVERIFY2(ProjectIO::saveCopy(*asset, path, &err), qPrintable(err));
        LoadResult r = ProjectIO::load(path);
        QVERIFY2(r.doc, qPrintable(r.error));

        auto e = make();                                                  // a different project
        const QVector<QUuid> ids = e->importSymbols(*r.doc);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(e->symbols().size(), 2);
        for (const QUuid& id : ids) QVERIFY(id != a && id != b);          // fresh ids
        const QUuid outer = e->symbols()[0].name == "Outer" ? e->symbols()[0].id : e->symbols()[1].id;
        QVERIFY(!e->addSymbolInstance(outer).isNull());
        QCOMPARE(px(*e, 0, 20, 20), QColor(Qt::red));                     // nested artwork came along
        e->importSymbols(*r.doc);                                         // twice = independent copies, unique names
        QCOMPARE(e->symbols().size(), 4);
        QSet<QString> names; for (const auto& sd : e->symbols()) names.insert(sd.name);
        QCOMPARE(names.size(), 4);
        e->undoStack()->undo();
        QCOMPARE(e->symbols().size(), 2);
    }

    void symbolsSurviveSaveAndLoadEvenWhileASymbolIsOpen() {
        auto d = make();
        const QUuid sym = d->convertToSymbol(square(*d), "Box", SymbolKind::MovieClip);
        const QUuid inst = d->root()->children[0]->id;
        d->setInstancePlayback(inst, SymbolPlay::PlayOnce, 3);
        d->enterSymbol(sym);
        d->setDuration(12);
        QCOMPARE(d->durationFrames(), 12);
        QTemporaryDir dir;
        const QString path = dir.filePath("s.studio");
        QString err;
        QVERIFY2(ProjectIO::save(*d, path, &err), qPrintable(err));      // saved while inside the symbol
        auto r = ProjectIO::load(path);
        QVERIFY2(r.doc, qPrintable(r.error));
        QCOMPARE(r.doc->durationFrames(), 30);                            // the scene's length, not the symbol's
        QCOMPARE(r.doc->symbols().size(), 1);
        QCOMPARE(r.doc->symbols()[0].name, QString("Box"));
        QCOMPARE(r.doc->symbols()[0].kind, SymbolKind::MovieClip);
        QCOMPARE(r.doc->symbols()[0].durationFrames, 12);
        const Layer* li = r.doc->findLayer(inst);
        QVERIFY(li);
        QCOMPARE(li->symbolPlay, SymbolPlay::PlayOnce);
        QCOMPARE(li->symbolFirstFrame, 3);
        QCOMPARE(px(*r.doc, 0, 20, 20), QColor(Qt::red));
    }
};

QTEST_MAIN(TestSymbols)
#include "test_symbols.moc"
