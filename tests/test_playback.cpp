#include <QtTest>

#include "TestEnv.h"
#include "Scene/Playback.h"

using namespace mynd;

class TestPlayback : public QObject {
    Q_OBJECT
    static std::unique_ptr<Document> make(int frames = 24, FrameRate fps = {24, 1}) {
        CompositionSettings s; s.width = 16; s.height = 16; s.fps = fps; s.durationFrames = frames;
        return std::make_unique<Document>(s);
    }
private slots:
    void frameAtIsExactAndDriftFree() {
        bool fin = false;
        // 24 fps: frame k starts at k * 41666.67 us; check the boundary on both sides.
        QCOMPARE(Playback::frameAt(41666, 0, 0, 99, {24, 1}, 100, false, &fin), 0);
        QCOMPARE(Playback::frameAt(41667, 0, 0, 99, {24, 1}, 100, false, &fin), 1);
        // 24000/1001 (23.976): one hour of playback lands on exactly the right frame.
        const qint64 hour = 3600LL * 1000000;
        QCOMPARE(Playback::frameAt(hour, 0, 0, 10'000'000, {24000, 1001}, 100, false, &fin), int(hour * 24000 / (1001LL * 1000000)));
        // 59.94 fps over ten hours, 200 % speed: compare with an independent long-double calculation
        const qint64 ten = 10LL * 3600 * 1000000;
        const long double exact = (long double)ten * 2 * 60000 / (1001.0L * 1000000);
        QVERIFY(qAbs(Playback::frameAt(ten, 0, 0, 100'000'000, {60000, 1001}, 200, false, &fin) - (long long)exact) <= 1);
        // 30 fps, half speed: 1 second advances 15 frames
        QCOMPARE(Playback::frameAt(1000000, 0, 0, 99, {30, 1}, 50, false, &fin), 15);
        // start offset
        QCOMPARE(Playback::frameAt(1000000, 10, 0, 99, {24, 1}, 100, false, &fin), 34);
    }
    void loopWrapsAndNonLoopStopsOnTheLastFrame() {
        bool fin = false;
        QCOMPARE(Playback::frameAt(1000000, 0, 0, 11, {24, 1}, 100, true, &fin), 0);    // 24 frames in 12-frame range
        QVERIFY(!fin);
        QCOMPARE(Playback::frameAt(500000, 0, 0, 11, {24, 1}, 100, true, &fin), 0);     // 12 frames => wrapped to start
        QCOMPARE(Playback::frameAt(458333, 0, 0, 11, {24, 1}, 100, false, &fin), 10);
        QVERIFY(!fin);
        QCOMPARE(Playback::frameAt(2000000, 0, 0, 11, {24, 1}, 100, false, &fin), 11);
        QVERIFY(fin);
        QCOMPARE(Playback::frameAt(1000, 5, 5, 5, {24, 1}, 100, true, &fin), 5);        // one-frame range
    }
    void playbackStepsTheDocumentFrameAndStopsAtTheEnd() {
        auto d = make(10);
        Playback pb; pb.setDocument(d.get());
        pb.setLoop(false);
        QSignalSpy spy(&pb, &Playback::playingChanged);
        d->setCurrentFrame(0);
        pb.play();
        QVERIFY(pb.isPlaying());
        pb.tick(0);       QCOMPARE(d->currentFrame(), 0);
        pb.tick(125000);  QCOMPARE(d->currentFrame(), 3);     // 3 frames at 24 fps
        pb.tick(300000);  QCOMPARE(d->currentFrame(), 7);
        QCOMPARE(pb.skippedFrames(), 5);                      // 1,2 and 4,5,6 were never shown (clock jumped)
        pb.tick(1000000);                                     // past the end
        QCOMPARE(d->currentFrame(), 9);
        QVERIFY(!pb.isPlaying());
        QCOMPARE(spy.size(), 2);                              // started, then stopped by itself
    }
    void workAreaLimitsPlaybackAndLoops() {
        auto d = make(100);
        d->setWorkArea(true, 20, 29);
        Playback pb; pb.setDocument(d.get()); pb.setLoop(true);
        d->setCurrentFrame(50);
        pb.play();                                            // outside the range: start at its beginning
        QCOMPARE(d->currentFrame(), 20);
        pb.tick(10 * 41667);                                  // 10 frames later: wrapped back to 20
        QCOMPARE(d->currentFrame(), 20);
        pb.tick(13 * 41667);
        QCOMPARE(d->currentFrame(), 23);
        pb.stop();
        QVERIFY(!pb.isPlaying());
        QCOMPARE(d->currentFrame(), 20);
    }
    void restartsFromTheBeginningWhenPlayedAtTheEnd() {
        auto d = make(10);
        Playback pb; pb.setDocument(d.get()); pb.setLoop(false);
        d->setCurrentFrame(9);
        pb.play();
        QCOMPARE(d->currentFrame(), 0);
        pb.pause();
        QVERIFY(!pb.isPlaying());
        pb.tick(500000);                                      // ignored while paused
        QCOMPARE(d->currentFrame(), 0);
    }
    void changingTheDocumentStopsPlayback() {
        auto d = make(); auto d2 = make();
        Playback pb; pb.setDocument(d.get());
        pb.play();
        QVERIFY(pb.isPlaying());
        pb.setDocument(d2.get());
        QVERIFY(!pb.isPlaying());
    }
    void realTimerAdvancesFrames() {
        auto d = make(1000, {100, 1});
        Playback pb; pb.setDocument(d.get());
        pb.play();
        QTest::qWait(250);
        pb.pause();
        QVERIFY2(d->currentFrame() >= 15 && d->currentFrame() <= 35, qPrintable(QString::number(d->currentFrame())));   // ~25 expected
    }
};

QTEST_MAIN(TestPlayback)
#include "test_playback.moc"
