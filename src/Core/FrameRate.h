#pragma once
#include <QtGlobal>

namespace mynd {

// Rational frame rate. Time is represented as integer frame counts + this rational,
// never accumulated floating-point seconds (foundation for the shared composition clock).
struct FrameRate {
    int num = 24;
    int den = 1;
    double fps() const { return den ? double(num) / den : 0.0; }
    bool isValid() const { return num > 0 && den > 0; }
    // Exact microsecond timestamp of frame n (rounded once, no drift accumulation).
    qint64 frameToMicros(qint64 frame) const { return (frame * den * 1000000LL + num / 2) / num; }
    bool operator==(const FrameRate& o) const { return num == o.num && den == o.den; }
};

}  // namespace mynd
