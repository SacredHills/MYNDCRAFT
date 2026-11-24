#include <QtTest>

#include "Scene/Animation.h"

using namespace mynd;

namespace {
const QUuid A = QUuid::createUuid(), B = QUuid::createUuid(), C = QUuid::createUuid();
AnimationState make() {
    AnimationState s;
    s.drawings = {{A, "1"}, {B, "2"}, {C, "3"}};
    return s;
}
// compact text form: "A0+3 B3+2" (drawing letter, start, length)
QString dump(const AnimationState& s) {
    QStringList l;
    for (const auto& e : s.exposures) {
        const QString d = e.drawing == A ? "A" : e.drawing == B ? "B" : e.drawing == C ? "C" : "_";
        l << QString("%1%2+%3").arg(d).arg(e.start).arg(e.length);
    }
    return l.join(' ');
}
}  // namespace

class TestAnimation : public QObject {
    Q_OBJECT
private slots:
    void setRangeOverwritesAndSplits() {
        auto s = make();
        s.setRange(0, 6, A);
        QCOMPARE(dump(s), QString("A0+6"));
        s.setRange(2, 2, B);                         // substitute the middle of a hold
        QCOMPARE(dump(s), QString("A0+2 B2+2 A4+2"));
        s.setRange(4, 2, B);                         // adjacent identical exposures merge
        QCOMPARE(dump(s), QString("A0+2 B2+4"));
        QCOMPARE(s.drawingAt(5), B);
        QVERIFY(s.drawingAt(6).isNull());
        QCOMPARE(s.endFrame(), 6);
    }
    void queriesHandleGapsAndBlanks() {
        auto s = make();
        s.setRange(0, 2, A); s.setRange(4, 2, B); s.setRange(2, 2, QUuid());   // explicit blank
        QCOMPARE(dump(s), QString("A0+2 _2+2 B4+2"));
        QVERIFY(s.at(2) != nullptr);
        QVERIFY(s.drawingAt(2).isNull());
        QVERIFY(s.at(6) == nullptr);
        s.clearRange(2, 2);
        QVERIFY(s.at(2) == nullptr);                 // gap, not an exposure
        QCOMPARE(s.indexAt(-1), -1);
        QCOMPARE(s.indexAt(100), -1);
    }
    void insertAndRemoveFramesRipple() {
        auto s = make();
        s.setRange(0, 3, A); s.setRange(3, 3, B);
        s.insertFrames(1, 2);                        // splits A around a 2-frame gap
        QCOMPARE(dump(s), QString("A0+1 A3+2 B5+3"));
        s.removeFrames(1, 2);                        // removing the gap re-joins A
        QCOMPARE(dump(s), QString("A0+3 B3+3"));
        s.removeFrames(2, 2);                        // delete across the A/B boundary
        QCOMPARE(dump(s), QString("A0+2 B2+2"));
        s.removeFrames(0, 100);
        QVERIFY(s.exposures.isEmpty());
    }
    void extendShortenDuplicate() {
        auto s = make();
        s.setRange(0, 2, A); s.setRange(2, 2, B);
        QVERIFY(s.extendExposure(0, 3));
        QCOMPARE(dump(s), QString("A0+5 B5+2"));     // later exposures are pushed, not overwritten
        QVERIFY(s.shortenExposure(1, 4));
        QCOMPARE(dump(s), QString("A0+1 B1+2"));
        QVERIFY(!s.shortenExposure(0, 1));           // never shrinks to nothing
        QVERIFY(s.duplicateExposure(1));
        QCOMPARE(dump(s), QString("A0+1 B1+4"));     // repeated hold of the same drawing merges
        QVERIFY(!s.extendExposure(50, 1));
    }
    void loopRepeatsPatternAndPushesRest() {
        auto s = make();
        s.setRange(0, 1, A); s.setRange(1, 2, B); s.setRange(3, 1, C);
        QVERIFY(s.loopRange(0, 2, 2));               // frames 0..2 repeated twice more
        QCOMPARE(dump(s), QString("A0+1 B1+2 A3+1 B4+2 A6+1 B7+2 C9+1"));
        QVERIFY(!s.loopRange(20, 25, 1));            // nothing there
        QVERIFY(!s.loopRange(0, 2, 0));
    }
    void markersAndSubstitution() {
        auto s = make();
        s.setRange(0, 4, A);
        QVERIFY(s.setMarker(2, PoseMarker::Key));
        QCOMPARE(s.at(3)->marker, PoseMarker::Key);
        QVERIFY(!s.setMarker(2, PoseMarker::Key));
        QVERIFY(s.replaceDrawing(1, B));             // whole exposure is substituted
        QCOMPARE(dump(s), QString("B0+4"));
        QVERIFY(!s.replaceDrawing(1, B));
        s.setRange(4, 2, A, PoseMarker::Key);
        s.setMarker(0, PoseMarker::Key);
        s.replaceDrawing(0, A);                      // now identical to the neighbour => merges
        QCOMPARE(dump(s), QString("A0+6"));
    }
    void neighborNavigationSkipsBlanksAndHolds() {
        auto s = make();
        s.setRange(0, 3, A); s.setRange(3, 2, QUuid()); s.setRange(5, 2, B); s.setRange(9, 1, C);
        QCOMPARE(s.neighborStart(1, 1), 5);
        QCOMPARE(s.neighborStart(5, 1), 9);
        QCOMPARE(s.neighborStart(9, 1), -1);
        QCOMPARE(s.neighborStart(6, -1), 0);
        QCOMPARE(s.neighborStart(0, -1), -1);
        QCOMPARE(s.neighborStart(8, -1), 5);         // from a gap
    }
    void namesAndExposureQuery() {
        auto s = make();
        QCOMPARE(s.nextDrawingName(), QString("4"));
        s.drawings.removeAt(0);
        QCOMPARE(s.nextDrawingName(), QString("1"));
        s.setRange(0, 1, B);
        QVERIFY(s.isDrawingExposed(B));
        QVERIFY(!s.isDrawingExposed(C));
    }
    void jsonRoundTripAndValidation() {
        auto s = make();
        s.setRange(0, 2, A, PoseMarker::Key); s.setRange(3, 2, B); s.setRange(5, 1, QUuid());
        AnimationState r; QString err;
        QVERIFY2(AnimationState::fromJson(s.toJson(), r, &err), qPrintable(err));
        QVERIFY(r == s);
        QJsonObject bad = s.toJson();
        QJsonArray ex = bad["exposures"].toArray();
        QJsonObject e0 = ex[0].toObject(); e0["length"] = 10; ex[0] = e0; bad["exposures"] = ex;   // overlaps next
        QVERIFY(!AnimationState::fromJson(bad, r, &err));
        QVERIFY(err.contains("overlapping"));
        bad = s.toJson(); ex = bad["exposures"].toArray(); e0 = ex[0].toObject(); e0["drawing"] = QUuid::createUuid().toString(QUuid::WithoutBraces); ex[0] = e0; bad["exposures"] = ex;
        QVERIFY(!AnimationState::fromJson(bad, r, &err));
        bad = s.toJson(); ex = bad["exposures"].toArray(); e0 = ex[0].toObject(); e0["start"] = -4; ex[0] = e0; bad["exposures"] = ex;
        QVERIFY(!AnimationState::fromJson(bad, r, &err));
    }
};

QTEST_APPLESS_MAIN(TestAnimation)
#include "test_animation.moc"
