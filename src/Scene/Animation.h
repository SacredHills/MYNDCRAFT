#pragma once
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

namespace mynd {

// Pose classification of an exposure, for pose-to-pose workflows.
enum class PoseMarker { None, Key, Breakdown, Inbetween };
QString poseMarkerKey(PoseMarker m);
PoseMarker poseMarkerFromKey(const QString& k);

struct DrawingMeta {
    QUuid id;          // persistent identity, unique within its layer
    QString name;      // shown on the exposure sheet ("1", "2", "A", ...)
    bool operator==(const DrawingMeta& o) const { return id == o.id && name == o.name; }
};

// One cell of the exposure sheet: `drawing` is shown for frames [start, start+length).
// A null drawing is an explicit blank exposure.
struct Exposure {
    int start = 0;
    int length = 1;
    QUuid drawing;
    PoseMarker marker = PoseMarker::None;
    int end() const { return start + length; }
    bool operator==(const Exposure& o) const {
        return start == o.start && length == o.length && drawing == o.drawing && marker == o.marker;
    }
};

// Timing data of one traditional-animation layer: the drawings it owns and when each is exposed.
// Exposures are kept sorted by start and never overlap; frames not covered by any exposure are blank.
// Drawings are separate from exposures so one drawing can be exposed many times (holds, reuse)
// without duplicating image data. All edits here are pure data operations (undo lives in Document).
class AnimationState {
public:
    QVector<DrawingMeta> drawings;
    QVector<Exposure> exposures;

    // ---- queries ----
    int indexAt(int frame) const;                       // exposure index covering `frame`, or -1
    const Exposure* at(int frame) const;
    QUuid drawingAt(int frame) const;                   // null if blank or uncovered
    int endFrame() const;                               // first frame after the last exposure (0 if empty)
    const DrawingMeta* drawing(const QUuid& id) const;
    QString nextDrawingName() const;                    // next free number: "1", "2", ...
    bool isDrawingExposed(const QUuid& id) const;
    // Start of the previous (dir < 0) or next (dir > 0) exposure that shows a drawing; -1 if none.
    int neighborStart(int frame, int dir) const;

    // ---- edits (return false when nothing could be done) ----
    void setRange(int start, int length, const QUuid& drawing, PoseMarker m = PoseMarker::None);  // overwrite
    void clearRange(int start, int length);             // frames become uncovered (blank), nothing shifts
    void insertFrames(int at, int count);               // ripple: later exposures move right; a spanning exposure splits
    void removeFrames(int at, int count);               // ripple: later exposures move left
    bool extendExposure(int frame, int count);          // lengthen the exposure at `frame`, pushing later ones
    bool shortenExposure(int frame, int count);         // shorten it (keeps at least 1 frame), pulling later ones
    bool duplicateExposure(int frame);                  // repeat the exposure right after itself
    bool loopRange(int from, int toInclusive, int times);  // repeat frames [from,to] `times` more times (ripple)
    bool setMarker(int frame, PoseMarker m);
    bool replaceDrawing(int frame, const QUuid& drawing);  // substitute the drawing of the whole exposure
    void normalize();                                   // sort, drop empties, merge identical neighbours

    bool operator==(const AnimationState& o) const { return drawings == o.drawings && exposures == o.exposures; }
    bool operator!=(const AnimationState& o) const { return !(*this == o); }

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& o, AnimationState& out, QString* err);
};

}  // namespace mynd
