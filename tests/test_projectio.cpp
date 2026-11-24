#include <QtTest>
#include <QPainter>
#include <zip.h>

#include "TestEnv.h"
#include "Core/Autosave.h"
#include "Core/ProjectIO.h"
#include "Core/Settings.h"

using namespace mynd;

namespace {
// Build a document exercising every Phase 1 data type.
std::unique_ptr<Document> sampleDoc() {
    CompositionSettings cs; cs.width = 120; cs.height = 80; cs.fps = {24000, 1001};
    cs.background = QColor(10, 20, 30, 255); cs.gridSize = 17;
    cs.guides = {{Qt::Horizontal, 12.0}, {Qt::Vertical, 99.0}};
    auto d = std::make_unique<Document>(cs);
    const QUuid r = d->addLayer(LayerType::Raster, "Paint");
    {
        QPainter p(&d->findLayer(r)->raster);
        p.setPen(Qt::NoPen); p.setBrush(QColor(200, 30, 60, 180)); p.drawEllipse(10, 10, 40, 30);
    }
    const QUuid g = d->addLayer(LayerType::Group, "Group");
    const QUuid v = d->addLayer(LayerType::Vector, "Vec", g);
    VectorShape e = VectorShape::makeEllipse(QRectF(5, 5, 50, 30));
    e.fill.type = Paint::LinearGradient; e.fill.gradStart = {5, 5}; e.fill.gradEnd = {55, 35};
    e.fill.stops = {{0, Qt::red}, {0.5, QColor(0, 255, 0, 128)}, {1, Qt::blue}};
    e.stroke.width = 4.5; e.stroke.color = QColor(1, 2, 3, 4); e.stroke.cap = Qt::FlatCap;
    const QUuid grp = QUuid::createUuid();
    e.groupId = grp;
    VectorShape pg = VectorShape::makePolygon({80, 40}, 20, 6, 0.3); pg.groupId = grp;
    VectorShape ln = VectorShape::makeLine({0, 0}, {50, 50});
    PathNode n; n.pos = {30, 30}; n.in = {-5, 2}; n.out = {7, -3}; n.smooth = true;
    VectorShape pen; pen.name = "pen"; pen.nodes = {PathNode{{0, 0}, {}, {10, 0}, false}, n}; pen.closed = false;
    d->setShapes(v, {e, pg, ln, pen}, "shapes");
    LayerProps p = d->findLayer(v)->props; p.opacity = 0.35; p.blend = BlendMode::Multiply; p.locked = true; d->setLayerProps(v, p);
    LayerProps p2 = d->findLayer(g)->props; p2.visible = false; d->setLayerProps(g, p2);
    d->markSaved();
    return d;
}
void compareLayers(const Layer* a, const Layer* b) {
    QCOMPARE(a->children.size(), b->children.size());
    for (size_t i = 0; i < a->children.size(); ++i) {
        const Layer *x = a->children[i].get(), *y = b->children[i].get();
        QCOMPARE(x->id, y->id);
        QCOMPARE(x->type, y->type);
        QVERIFY(x->props == y->props);
        QCOMPARE(x->shapes.size(), y->shapes.size());
        for (int s = 0; s < x->shapes.size(); ++s) QVERIFY2(x->shapes[s] == y->shapes[s], "vector shape differs after roundtrip");
        if (x->type == LayerType::Raster) QCOMPARE(x->raster, y->raster);
        compareLayers(x, y);
    }
}
bool rewriteZip(const QString& path, const std::function<void(zip_t*)>& fn) {
    int err = 0;
    zip_t* z = zip_open(QFile::encodeName(path).constData(), 0, &err);
    if (!z) return false;
    fn(z);
    return zip_close(z) == 0;
}
}  // namespace

class TestProjectIO : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString path(const QString& n) { return dir.filePath(n); }
private slots:
    void roundTripPreservesEverything() {
        auto d = sampleDoc();
        QString err;
        QVERIFY2(ProjectIO::save(*d, path("a.studio"), &err), qPrintable(err));
        QVERIFY(!d->isModified());
        QCOMPARE(d->filePath(), path("a.studio"));
        LoadResult r = ProjectIO::load(path("a.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(";")));
        const auto& c = r.doc->comp();
        QCOMPARE(c.width, 120); QCOMPARE(c.height, 80);
        QCOMPARE(c.fps.num, 24000); QCOMPARE(c.fps.den, 1001);
        QCOMPARE(c.background, QColor(10, 20, 30));
        QCOMPARE(c.gridSize, 17.0);
        QCOMPARE(c.guides.size(), 2);
        QVERIFY(c.guides[1] == (Guide{Qt::Vertical, 99.0}));
        QCOMPARE(r.doc->docId(), d->docId());
        compareLayers(d->root(), r.doc->root());
        QVERIFY(ProjectIO::validate(*r.doc).isEmpty());
        QVERIFY(!r.doc->isModified());
        // Editable after reopening: vector geometry still has its bezier data and can be edited + undone.
        Layer* v = r.doc->findLayer(d->root()->children[1]->children[0]->id);
        ShapeList s = v->shapes; s[0].translate({3, 3});
        r.doc->setShapes(v->id, s, "edit");
        QVERIFY(r.doc->isModified());
        r.doc->undoStack()->undo();
        QVERIFY(v->shapes[0] == d->root()->children[1]->children[0]->shapes[0]);
    }
    void saveKeepsBackupOfPreviousVersion() {
        auto d = sampleDoc();
        QString err;
        QVERIFY(ProjectIO::save(*d, path("b.studio"), &err));
        QVERIFY(!QFile::exists(path("b.studio.bak")));
        d->addLayer(LayerType::Raster, "Second");
        QVERIFY(ProjectIO::save(*d, path("b.studio"), &err));
        QVERIFY(QFile::exists(path("b.studio.bak")));
        QCOMPARE(ProjectIO::load(path("b.studio.bak")).doc->layerCount(), d->layerCount() - 1);
        QVERIFY(!QFile::exists(path("b.studio.tmp")));
    }
    void missingRasterAssetWarnsAndLayerIsBlank() {
        auto d = sampleDoc();
        QString err;
        QVERIFY(ProjectIO::save(*d, path("m.studio"), &err));
        QVERIFY(rewriteZip(path("m.studio"), [&](zip_t* z) {
            const QString name = "assets/raster/" + d->root()->children[0]->id.toString(QUuid::WithoutBraces) + ".png";
            zip_int64_t idx = zip_name_locate(z, name.toUtf8().constData(), 0);
            QVERIFY(idx >= 0);
            zip_delete(z, idx);
        }));
        LoadResult r = ProjectIO::load(path("m.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings[0].contains("Paint"));
        QCOMPARE(r.doc->root()->children[0]->raster, r.doc->newRasterImage());
    }
    void newerFormatVersionIsRejectedClearly() {
        auto d = sampleDoc();
        QString err;
        QVERIFY(ProjectIO::save(*d, path("f.studio"), &err));
        QVERIFY(rewriteZip(path("f.studio"), [&](zip_t* z) {
            QJsonObject o{{"format", kProjectFormatName}, {"version", 99}};
            static QByteArray buf; buf = QJsonDocument(o).toJson();
            zip_source_t* src = zip_source_buffer(z, buf.constData(), buf.size(), 0);
            zip_file_add(z, "manifest.json", src, ZIP_FL_OVERWRITE);
        }));
        LoadResult r = ProjectIO::load(path("f.studio"));
        QVERIFY(!r.doc);
        QVERIFY2(r.error.contains("newer version"), qPrintable(r.error));
        QVERIFY(!ProjectIO::migrate(*std::make_unique<QJsonObject>().get(), 0, &err));
    }
    void corruptAndForeignFilesGiveErrors() {
        QFile f(path("junk.studio")); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("this is not a zip"); f.close();
        LoadResult a = ProjectIO::load(path("junk.studio"));
        QVERIFY(!a.doc); QVERIFY(!a.error.isEmpty());
        LoadResult b = ProjectIO::load(path("does-not-exist.studio"));
        QVERIFY(!b.doc); QVERIFY(b.error.contains("not found"));
        // A valid zip that is not a project.
        int err = 0; zip_t* z = zip_open(QFile::encodeName(path("other.zip")).constData(), ZIP_CREATE, &err);
        static QByteArray hello = "hi";
        zip_file_add(z, "readme.txt", zip_source_buffer(z, hello.constData(), hello.size(), 0), 0);
        zip_close(z);
        LoadResult c = ProjectIO::load(path("other.zip"));
        QVERIFY(!c.doc); QVERIFY(c.error.contains("manifest"));
    }
    void absurdCanvasSizeRejected() {
        auto d = sampleDoc();
        QString err;
        QVERIFY(ProjectIO::save(*d, path("big.studio"), &err));
        QVERIFY(rewriteZip(path("big.studio"), [&](zip_t* z) {
            QJsonObject o{{"width", 1000000}, {"height", 10}, {"layers", QJsonArray()}};
            static QByteArray buf; buf = QJsonDocument(o).toJson();
            zip_file_add(z, "composition.json", zip_source_buffer(z, buf.constData(), buf.size(), 0), ZIP_FL_OVERWRITE);
        }));
        LoadResult r = ProjectIO::load(path("big.studio"));
        QVERIFY(!r.doc);
        QVERIFY2(r.error.contains("Invalid canvas size"), qPrintable(r.error));
    }
    void validateDetectsDuplicateIds() {
        auto d = sampleDoc();
        d->root()->children.push_back(d->root()->children[0]);  // same layer twice
        QVERIFY(!ProjectIO::validate(*d).isEmpty());
    }
    void autosaveWritesRecoverableCopyAndCleansUp() {
        QDir(settings::autosaveDir()).removeRecursively();
        auto d = sampleDoc();
        Autosaver as;
        as.attach(d.get());
        QVERIFY(!as.saveNow());                // unmodified: nothing to write
        d->addLayer(LayerType::Raster, "Unsaved work");
        QVERIFY(as.saveNow());
        auto found = Autosaver::findRecoverable();
        QCOMPARE(found.size(), 1);
        LoadResult r = ProjectIO::load(found[0].autosavePath);
        QVERIFY2(r.doc, qPrintable(r.error));
        QCOMPARE(r.doc->layerCount(), d->layerCount());
        as.detach(true);                        // clean close removes it
        QVERIFY(Autosaver::findRecoverable().isEmpty());
        // Crash simulation: detach(false) leaves the file for recovery on next start.
        Autosaver as2; as2.attach(d.get()); d->addLayer(LayerType::Raster, "More"); QVERIFY(as2.saveNow()); as2.detach(false);
        QCOMPARE(Autosaver::findRecoverable().size(), 1);
        Autosaver::discard(Autosaver::findRecoverable()[0]);
        QVERIFY(Autosaver::findRecoverable().isEmpty());
    }
    void backgroundAutosaveWritesTheSnapshotAndDetachWaitsForIt() {
        QDir(settings::autosaveDir()).removeRecursively();
        auto d = sampleDoc();
        Autosaver as; as.attach(d.get());
        d->addLayer(LayerType::Raster, "Unsaved work");
        const int layers = d->layerCount();
        QVERIFY(as.saveInBackground());
        QVERIFY(as.busy());
        d->addLayer(LayerType::Raster, "Added while writing");        // the file holds the state at snapshot time
        QVERIFY(!as.saveInBackground());                               // one write at a time
        QTRY_VERIFY(!as.busy());
        auto found = Autosaver::findRecoverable();
        QCOMPARE(found.size(), 1);
        LoadResult r = ProjectIO::load(found[0].autosavePath);
        QVERIFY2(r.doc, qPrintable(r.error));
        QCOMPARE(r.doc->layerCount(), layers);
        QVERIFY(as.saveInBackground());
        as.detach(true);                                               // must wait for the running write, then delete the file
        QVERIFY(Autosaver::findRecoverable().isEmpty());
    }
    void settingsPersist() {
        settings::setAutosaveIntervalSeconds(45);
        QCOMPARE(settings::autosaveIntervalSeconds(), 45);
        settings::setAutosaveIntervalSeconds(1);
        QCOMPARE(settings::autosaveIntervalSeconds(), 10);    // clamped
        settings::addRecentFile("/x/a.studio"); settings::addRecentFile("/x/b.studio"); settings::addRecentFile("/x/a.studio");
        QCOMPARE(settings::recentFiles().mid(0, 2), (QStringList{"/x/a.studio", "/x/b.studio"}));
        settings::saveWorkspaceState("Mine", "abc");
        QVERIFY(settings::workspaceNames().contains("Mine"));
        settings::removeWorkspace("Mine");
        QVERIFY(!settings::workspaceNames().contains("Mine"));
    }

    void textLayerRoundTrips() {
        CompositionSettings cs; cs.width = 100; cs.height = 60;
        Document d(cs);
        TextContent c; c.text = "Line one\nLine two"; c.family = "DejaVu Sans"; c.sizePx = 31; c.bold = true; c.italic = true;
        c.color = QColor(10, 200, 30, 180); c.tracking = 1.5; c.align = TextAlign::Center; c.pos = {12.5, 7}; c.boxWidth = 80;
        const QUuid id = d.addTextLayer(c, "My text");
        QString err;
        QVERIFY2(ProjectIO::save(d, path("t.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(path("t.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY(r.warnings.isEmpty());
        const Layer* l = r.doc->findLayer(id);
        QVERIFY(l);
        QCOMPARE(l->type, LayerType::Text);
        QVERIFY(l->textContent == c);
        QCOMPARE(l->props.name, QString("My text"));
    }

    void animationLayersRoundTripWithDrawingsAndTiming() {
        CompositionSettings cs; cs.width = 32; cs.height = 24; cs.fps = {25, 1};
        Document d(cs);
        const QUuid lid = d.addAnimationLayer("Walk");
        Layer* l = d.findLayer(lid);
        const QUuid a = d.createDrawing(lid, 0, false);
        const QUuid b = d.createDrawing(lid, 3, false);
        l->drawingImages[a].fill(QColor(200, 0, 0));
        l->drawingImages[b].fill(QColor(0, 0, 200));
        AnimationState s = l->anim;
        s.setRange(0, 3, a, PoseMarker::Key); s.setRange(3, 2, b, PoseMarker::Breakdown); s.setRange(5, 1, QUuid());
        d.editAnimation(lid, s, "t");
        d.setDuration(30); d.setWorkArea(true, 2, 12);
        QString err;
        QVERIFY2(ProjectIO::save(d, path("anim.studio"), &err), qPrintable(err));
        LoadResult r = ProjectIO::load(path("anim.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(";")));
        const Layer* rl = r.doc->findLayer(lid);
        QVERIFY(rl);
        QCOMPARE(rl->type, LayerType::Animation);
        QVERIFY(rl->anim == l->anim);
        QCOMPARE(QColor(rl->drawingImages.at(a).pixel(3, 3)), QColor(200, 0, 0));
        QCOMPARE(QColor(rl->drawingImages.at(b).pixel(3, 3)), QColor(0, 0, 200));
        QCOMPARE(r.doc->durationFrames(), 30);
        QVERIFY(r.doc->comp().workEnabled);
        QCOMPARE(r.doc->comp().workIn, 2); QCOMPARE(r.doc->comp().workOut, 12);
        QCOMPARE(r.doc->comp().fps.num, 25);
    }
    void missingDrawingAssetIsReportedAndLoadsBlank() {
        CompositionSettings cs; cs.width = 16; cs.height = 16;
        Document d(cs);
        const QUuid lid = d.addAnimationLayer("A");
        const QUuid a = d.createDrawing(lid, 0, false);
        QString err;
        QVERIFY(ProjectIO::save(d, path("miss.studio"), &err));
        // Rewrite the archive without the drawing PNG.
        int ze = 0;
        zip_t* z = zip_open(QFile::encodeName(path("miss.studio")).constData(), 0, &ze);
        QVERIFY(z);
        const QByteArray nm = ("assets/drawings/" + lid.toString(QUuid::WithoutBraces) + "/" + a.toString(QUuid::WithoutBraces) + ".png").toUtf8();
        const zip_int64_t idx = zip_name_locate(z, nm.constData(), 0);
        QVERIFY(idx >= 0);
        QCOMPARE(zip_delete(z, idx), 0);
        QCOMPARE(zip_close(z), 0);
        LoadResult r = ProjectIO::load(path("miss.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QVERIFY(!r.warnings.isEmpty());
        QVERIFY(r.warnings.first().contains("blank"));
        QVERIFY(r.doc->findLayer(lid)->imageAt(0));
    }
    void versionOneProjectsStillOpen() {
        CompositionSettings cs; cs.width = 16; cs.height = 16;
        Document d(cs); d.addLayer(LayerType::Raster, "R");
        QString err;
        QVERIFY(ProjectIO::save(d, path("v1.studio"), &err));
        int ze = 0;
        zip_t* z = zip_open(QFile::encodeName(path("v1.studio")).constData(), 0, &ze);
        QVERIFY(z);
        QJsonObject m{{"format", kProjectFormatName}, {"version", 1}, {"composition", "composition.json"}, {"docId", d.docId().toString(QUuid::WithoutBraces)}};
        static QByteArray mj; mj = QJsonDocument(m).toJson();
        zip_source_t* src = zip_source_buffer(z, mj.constData(), mj.size(), 0);
        QVERIFY(zip_file_add(z, "manifest.json", src, ZIP_FL_OVERWRITE) >= 0);
        QCOMPARE(zip_close(z), 0);
        LoadResult r = ProjectIO::load(path("v1.studio"));
        QVERIFY2(r.doc, qPrintable(r.error));
        QCOMPARE(r.doc->durationFrames(), 48);                          // default for files without a timeline
        QCOMPARE(r.doc->layerCount(), 1);
    }
};

QTEST_MAIN(TestProjectIO)
#include "test_projectio.moc"
