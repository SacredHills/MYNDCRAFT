#include <QtTest>
#include <QTemporaryDir>
#include <cmath>

#include "TestEnv.h"
#include "Audio/AudioClip.h"
#include "Core/Settings.h"
#include "UI/AudioEngine.h"
#include "UI/MainWindow.h"
#include "UI/PropertiesPanel.h"
#include "UI/TimelinePanel.h"
#include "UI/Workspaces.h"

using namespace mynd;

// Phase 10 in the UI: import, timeline clip bar, properties, mixdown, the mixing device behind playback.
class TestUiAudio : public QObject {
    Q_OBJECT
    std::unique_ptr<MainWindow> w;
    EditorSession* s = nullptr;
    Document* doc = nullptr;
    QTemporaryDir dir;

    QString writeWav(const QString& name, double seconds, double level) {
        std::vector<float> v(size_t(seconds * kAudioRate) * 2, float(level));
        const QString p = dir.filePath(name);
        QFile f(p); f.open(QIODevice::WriteOnly); f.write(encodeWav(v));
        return p;
    }
    PropertiesPanel* panel() { return w->findChild<PropertiesPanel*>(); }
    TimelineView* tv() { return w->findChild<TimelinePanel*>()->view(); }
    Layer* audioLayer() { for (auto& c : doc->sceneRoot()->children) if (c->type == LayerType::Audio) return c.get(); return nullptr; }

private slots:
    void init() {
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        w = std::make_unique<MainWindow>();
        CompositionSettings cs; cs.width = 200; cs.height = 150; cs.durationFrames = 96; cs.fps = {24, 1};
        w->newDocument(cs, false);
        w->resize(1500, 900);
        w->show();
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        w->workspaces()->apply("Animation");
        for (const QString& n : WorkspaceManager::presetNames()) settings::removePresetLayout(n);
        QApplication::processEvents();
        s = w->session(); doc = w->document();
    }
    void cleanup() { doc->undoStack()->clear(); doc->markSaved(); w->close(); w.reset(); }

    void importCreatesAnAudioLayerAtThePlayheadAndUndoRemovesIt() {
        const QString p = writeWav("a.wav", 1.0, 0.5);
        doc->setCurrentFrame(5);
        QString err;
        QVERIFY2(w->importAudio(p, &err), qPrintable(err));
        Layer* l = audioLayer();
        QVERIFY(l);
        QCOMPARE(l->audioParams.startFrame, 5);
        QCOMPARE(l->audio->name, QString("a"));
        QCOMPARE(s->activeLayerId(), l->id);
        doc->undoStack()->undo();
        QVERIFY(!audioLayer());
        QVERIFY(!w->importAudio(dir.filePath("missing.wav"), &err));
        QVERIFY(!err.isEmpty());
        QFile bad(dir.filePath("bad.mp3")); bad.open(QIODevice::WriteOnly); bad.write("not audio at all, just text"); bad.close();
        QVERIFY(!w->importAudio(dir.filePath("bad.mp3"), &err));
        QVERIFY(!audioLayer());
        QVERIFY(MainWindow::isAudioFile("x.MP3") && MainWindow::isAudioFile("x.wav") && !MainWindow::isAudioFile("x.png"));
    }
    void propertiesPanelEditsPlacementAndVolume() {
        QVERIFY(w->importAudio(writeWav("a.wav", 2.0, 0.5)));
        Layer* l = audioLayer();
        PropertiesPanel* p = panel();
        QVERIFY(p->audioBox()->isVisibleTo(p));
        p->audioTrimIn()->setValue(0.5);
        QCOMPARE(audioLayer()->audioParams.trimIn, 0.5);
        p->audioStart()->setValue(11);                                       // 1-based in the UI
        QCOMPARE(audioLayer()->audioParams.startFrame, 10);
        p->audioLoop()->setChecked(true);
        QVERIFY(audioLayer()->audioParams.loop);
        QDoubleSpinBox* vol = p->field("audio.volume", 0);
        QVERIFY(vol);
        QCOMPARE(vol->value(), 100.0);
        vol->setValue(50);
        QCOMPARE(l->tracks.value("audio.volume").base.c[0], 50.0);
        p->stopwatch("audio.volume")->click();                                // animate
        QVERIFY(audioLayer()->tracks.value("audio.volume").animated());
        doc->setCurrentFrame(20);
        vol->setValue(150);                                                   // changes at another frame make a key
        QCOMPARE(audioLayer()->tracks.value("audio.volume").keys.size(), 2);
        QVERIFY(std::fabs(valueOf(audioLayer()->tracks, *findPropertyDef("audio.volume"), 20).c[0] - 150) < 1e-9);
        // other layer types hide the audio controls again
        s->addLayerAboveActive(LayerType::Vector, "V");
        QVERIFY(!p->audioBox()->isVisibleTo(p));
    }
    void timelineBarMovesAndTrimsWithOneUndoStepEach() {
        QVERIFY(w->importAudio(writeWav("a.wav", 2.0, 0.5)));                 // 48 frames at 24 fps
        TimelineView* t = tv();
        const QUuid id = audioLayer()->id;
        const int row = t->rowOfLayer(id);
        QVERIFY(row >= 0);
        const int before = doc->undoStack()->index();
        QWidget* vp = t->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, t->cellCenter(10, row));
        QTest::mouseMove(vp, t->cellCenter(14, row));
        QTest::mouseMove(vp, t->cellCenter(18, row));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, t->cellCenter(18, row));
        QCOMPARE(audioLayer()->audioParams.startFrame, 8);                    // moved by 8 frames
        QCOMPARE(doc->undoStack()->index(), before + 1);                      // the drag is a single undo step
        // trim the end: drag the right edge 12 frames left (0.5 s)
        const int cw = t->cellCenter(1, row).x() - t->cellCenter(0, row).x();
        const QPoint edge = t->cellCenter(8 + 47, row) + QPoint(cw / 2 - 2, 0);   // inside the last cell of the bar, next to its right edge
        QTest::mousePress(vp, Qt::LeftButton, {}, edge);
        QTest::mouseMove(vp, edge - QPoint(6 * cw, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, edge - QPoint(6 * cw, 0));
        QVERIFY2(audioLayer()->audioParams.trimOut > 0 && audioLayer()->audioParams.trimOut < 2.0, qPrintable(QString::number(audioLayer()->audioParams.trimOut)));
        QCOMPARE(doc->undoStack()->index(), before + 2);
        doc->undoStack()->undo();
        QCOMPARE(audioLayer()->audioParams.trimOut, 0.0);
        doc->undoStack()->undo();
        QCOMPARE(audioLayer()->audioParams.startFrame, 0);
    }
    void mixdownWritesAWavOfTheComposition() {
        QString err;
        QVERIFY(!w->exportAudioMixdown(dir.filePath("none.wav"), &err));        // nothing audible yet
        QVERIFY(w->importAudio(writeWav("a.wav", 1.0, 0.25)));
        QVERIFY(w->importAudio(writeWav("b.wav", 1.0, 0.25)));
        const QString out = dir.filePath("mix.wav");
        QVERIFY2(w->exportAudioMixdown(out, &err), qPrintable(err));
        auto c = decodeAudioFile(out, &err);
        QVERIFY2(c, qPrintable(err));
        QCOMPARE(c->frames(), qint64(4 * kAudioRate));                         // 96 frames at 24 fps
        QVERIFY(std::fabs(c->pcm[size_t(kAudioRate / 2) * 2] - 0.5f) < 0.001f);   // the two layers add
        QCOMPARE(c->pcm[size_t(2 * kAudioRate) * 2], 0.0f);                    // silence after the clips end
    }
    void theMixingDeviceFeedsTheSoundCard() {
        QVERIFY(w->importAudio(writeWav("a.wav", 1.0, 0.5)));
        AudioEngine* e = s->audio();
        QVERIFY(e);
        const auto sc = e->currentScene();
        QVERIFY(sc && sc->sources.size() == 1);
        AudioMixDevice* d = e->device();
        d->setScene(sc);
        d->setPosition(kAudioRate / 2, -1);
        QByteArray buf(8 * 100, 0);
        QCOMPARE(d->read(buf.data(), buf.size()), qint64(buf.size()));
        const float* f = reinterpret_cast<const float*>(buf.constData());
        QVERIFY(std::fabs(f[0] - 0.5f) < 0.001f && std::fabs(f[199] - 0.5f) < 0.001f);
        QCOMPARE(d->position(), qint64(kAudioRate / 2 + 100));
        d->setPosition(kAudioRate - 50, -1);                                    // runs past the end of the 1 s clip
        d->read(buf.data(), buf.size());
        QVERIFY(std::fabs(f[0] - 0.5f) < 0.001f);
        QCOMPARE(f[2 * 60], 0.0f);                                              // silence after the clip
        d->setPosition(0, 40);                                                  // a scrub snippet: only 40 frames carry sound
        d->read(buf.data(), buf.size());
        QVERIFY(std::fabs(f[2 * 39] - 0.5f) < 0.001f);
        QCOMPARE(f[2 * 40], 0.0f);
        d->int16Output = true;                                                  // 16-bit fallback for devices without float
        d->setPosition(0, -1);
        QByteArray b16(4 * 10, 0);
        QCOMPARE(d->read(b16.data(), b16.size()), qint64(b16.size()));
        QVERIFY(std::abs(reinterpret_cast<const qint16*>(b16.constData())[0] - 16384) < 3);
        d->int16Output = false;
    }
    void editsReachTheEngineSceneAndMutedLayersDropOut() {
        QVERIFY(w->importAudio(writeWav("a.wav", 1.0, 0.5)));
        AudioEngine* e = s->audio();
        const QUuid id = audioLayer()->id;
        LayerProps pr = doc->findLayer(id)->props; pr.visible = false; doc->setLayerProps(id, pr);
        QVERIFY(e->currentScene()->sources.empty());
        pr.visible = true; doc->setLayerProps(id, pr);
        QCOMPARE(e->currentScene()->sources.size(), size_t(1));
        // saving and reopening keeps the sound
        const QString proj = dir.filePath("p.studio");
        QString err;
        QVERIFY2(w->saveFile(proj, &err), qPrintable(err));
        QVERIFY2(w->openFile(proj, &err), qPrintable(err));
        doc = w->document();
        QVERIFY(audioLayer() && audioLayer()->audio);
        QCOMPARE(audioLayer()->audio->frames(), qint64(kAudioRate));
    }
};

QTEST_MAIN(TestUiAudio)
#include "test_ui_audio.moc"
