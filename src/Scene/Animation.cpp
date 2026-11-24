#include "Scene/Animation.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace mynd {

QString poseMarkerKey(PoseMarker m) {
    switch (m) {
    case PoseMarker::Key: return "key";
    case PoseMarker::Breakdown: return "breakdown";
    case PoseMarker::Inbetween: return "inbetween";
    case PoseMarker::None: break;
    }
    return "none";
}
PoseMarker poseMarkerFromKey(const QString& k) {
    if (k == "key") return PoseMarker::Key;
    if (k == "breakdown") return PoseMarker::Breakdown;
    if (k == "inbetween") return PoseMarker::Inbetween;
    return PoseMarker::None;
}

int AnimationState::indexAt(int frame) const {
    // exposures are sorted: binary search for the last start <= frame
    int lo = 0, hi = exposures.size() - 1, found = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (exposures[mid].start <= frame) { found = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return (found >= 0 && frame < exposures[found].end()) ? found : -1;
}
const Exposure* AnimationState::at(int frame) const {
    const int i = indexAt(frame);
    return i < 0 ? nullptr : &exposures[i];
}
QUuid AnimationState::drawingAt(int frame) const {
    const Exposure* e = at(frame);
    return e ? e->drawing : QUuid();
}
int AnimationState::endFrame() const { return exposures.isEmpty() ? 0 : exposures.last().end(); }
const DrawingMeta* AnimationState::drawing(const QUuid& id) const {
    for (const auto& d : drawings) if (d.id == id) return &d;
    return nullptr;
}
QString AnimationState::nextDrawingName() const {
    QSet<QString> used;
    for (const auto& d : drawings) used.insert(d.name);
    int n = 1;
    while (used.contains(QString::number(n))) ++n;
    return QString::number(n);
}
bool AnimationState::isDrawingExposed(const QUuid& id) const {
    for (const auto& e : exposures) if (e.drawing == id) return true;
    return false;
}
int AnimationState::neighborStart(int frame, int dir) const {
    const int cur = indexAt(frame);
    if (dir < 0) {
        const int limit = cur >= 0 ? exposures[cur].start : frame;
        for (int i = exposures.size() - 1; i >= 0; --i)
            if (exposures[i].start < limit && !exposures[i].drawing.isNull()) return exposures[i].start;
    } else {
        for (const auto& e : exposures)
            if (e.start > frame && !e.drawing.isNull()) return e.start;
    }
    return -1;
}

void AnimationState::normalize() {
    QVector<Exposure> v;
    for (const auto& e : exposures) if (e.length > 0 && e.start >= 0) v.append(e);
    std::stable_sort(v.begin(), v.end(), [](const Exposure& a, const Exposure& b) { return a.start < b.start; });
    QVector<Exposure> out;
    for (const auto& e : v) {
        if (!out.isEmpty() && out.last().end() == e.start && out.last().drawing == e.drawing &&
            out.last().marker == e.marker) {
            out.last().length += e.length;
        } else {
            out.append(e);
        }
    }
    exposures = out;
}

void AnimationState::clearRange(int start, int length) {
    if (length <= 0) return;
    const int end = start + length;
    QVector<Exposure> out;
    for (const auto& e : exposures) {
        if (e.end() <= start || e.start >= end) { out.append(e); continue; }
        if (e.start < start) { Exposure l = e; l.length = start - e.start; out.append(l); }
        if (e.end() > end) { Exposure r = e; r.start = end; r.length = e.end() - end; out.append(r); }
    }
    exposures = out;
    normalize();
}

void AnimationState::setRange(int start, int length, const QUuid& drawing, PoseMarker m) {
    if (length <= 0 || start < 0) return;
    clearRange(start, length);
    Exposure e; e.start = start; e.length = length; e.drawing = drawing; e.marker = m;
    exposures.append(e);
    normalize();
}

void AnimationState::insertFrames(int at, int count) {
    if (count <= 0) return;
    QVector<Exposure> out;
    for (const auto& e : exposures) {
        if (e.end() <= at) out.append(e);
        else if (e.start >= at) { Exposure s = e; s.start += count; out.append(s); }
        else {  // spans `at`: split around the inserted gap
            Exposure l = e; l.length = at - e.start; out.append(l);
            Exposure r = e; r.start = at + count; r.length = e.end() - at; out.append(r);
        }
    }
    exposures = out;
}

void AnimationState::removeFrames(int at, int count) {
    if (count <= 0) return;
    const int end = at + count;
    QVector<Exposure> out;
    for (const auto& e : exposures) {
        if (e.end() <= at) { out.append(e); continue; }
        if (e.start >= end) { Exposure s = e; s.start -= count; out.append(s); continue; }
        if (e.start < at) { Exposure l = e; l.length = at - e.start; out.append(l); }
        if (e.end() > end) { Exposure r = e; r.start = at; r.length = e.end() - end; out.append(r); }
    }
    exposures = out;
    normalize();
}

bool AnimationState::extendExposure(int frame, int count) {
    const int i = indexAt(frame);
    if (i < 0 || count <= 0) return false;
    const int end = exposures[i].end();
    insertFrames(end, count);
    exposures[i].length += count;
    normalize();
    return true;
}

bool AnimationState::shortenExposure(int frame, int count) {
    const int i = indexAt(frame);
    if (i < 0 || count <= 0 || exposures[i].length <= count) return false;
    const int end = exposures[i].end();
    removeFrames(end - count, count);
    return true;
}

bool AnimationState::duplicateExposure(int frame) {
    const int i = indexAt(frame);
    if (i < 0) return false;
    const Exposure e = exposures[i];
    insertFrames(e.end(), e.length);
    Exposure c = e; c.start = e.end();
    exposures.append(c);
    normalize();
    return true;
}

bool AnimationState::loopRange(int from, int toInclusive, int times) {
    if (times <= 0 || from < 0 || toInclusive < from) return false;
    const int len = toInclusive - from + 1;
    QVector<Exposure> seg;
    for (const auto& e : exposures) {
        const int s = std::max(e.start, from), t = std::min(e.end(), toInclusive + 1);
        if (t > s) { Exposure c = e; c.start = s; c.length = t - s; seg.append(c); }
    }
    if (seg.isEmpty()) return false;
    insertFrames(toInclusive + 1, len * times);
    for (int k = 1; k <= times; ++k)
        for (Exposure c : seg) { c.start += k * len; exposures.append(c); }
    normalize();
    return true;
}

bool AnimationState::setMarker(int frame, PoseMarker m) {
    const int i = indexAt(frame);
    if (i < 0 || exposures[i].marker == m) return false;
    exposures[i].marker = m;
    normalize();
    return true;
}

bool AnimationState::replaceDrawing(int frame, const QUuid& d) {
    const int i = indexAt(frame);
    if (i < 0 || exposures[i].drawing == d) return false;
    exposures[i].drawing = d;
    normalize();
    return true;
}

QJsonObject AnimationState::toJson() const {
    QJsonArray ds, es;
    for (const auto& d : drawings) ds.append(QJsonObject{{"id", d.id.toString(QUuid::WithoutBraces)}, {"name", d.name}});
    for (const auto& e : exposures) {
        QJsonObject o{{"start", e.start}, {"length", e.length}, {"marker", poseMarkerKey(e.marker)}};
        if (!e.drawing.isNull()) o["drawing"] = e.drawing.toString(QUuid::WithoutBraces);
        es.append(o);
    }
    return QJsonObject{{"drawings", ds}, {"exposures", es}};
}

bool AnimationState::fromJson(const QJsonObject& o, AnimationState& out, QString* err) {
    out = {};
    QSet<QUuid> ids;
    for (const auto& v : o["drawings"].toArray()) {
        const QJsonObject d = v.toObject();
        const QUuid id(d["id"].toString());
        if (id.isNull() || ids.contains(id)) { if (err) *err = "Animation layer has an invalid or duplicate drawing id"; return false; }
        ids.insert(id);
        out.drawings.append({id, d["name"].toString()});
    }
    constexpr int kMaxFrame = 10'000'000;
    for (const auto& v : o["exposures"].toArray()) {
        const QJsonObject e = v.toObject();
        Exposure x;
        x.start = e["start"].toInt(-1);
        x.length = e["length"].toInt(0);
        if (x.start < 0 || x.length < 1 || x.start > kMaxFrame || x.length > kMaxFrame) {
            if (err) *err = "Animation layer has an invalid exposure";
            return false;
        }
        if (e.contains("drawing")) {
            x.drawing = QUuid(e["drawing"].toString());
            if (!ids.contains(x.drawing)) { if (err) *err = "Exposure refers to an unknown drawing"; return false; }
        }
        x.marker = poseMarkerFromKey(e["marker"].toString());
        out.exposures.append(x);
    }
    // Overlapping exposures mean a corrupt file; normalize() would silently hide that.
    QVector<Exposure> sorted = out.exposures;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Exposure& a, const Exposure& b) { return a.start < b.start; });
    for (int i = 1; i < sorted.size(); ++i)
        if (sorted[i].start < sorted[i - 1].end()) { if (err) *err = "Animation layer has overlapping exposures"; return false; }
    out.normalize();
    return true;
}

}  // namespace mynd
