#pragma once
#include "Scene/VectorShape.h"

namespace mynd {

// Shape tweening: the outline of `a` turns into the outline of `b` as t goes 0 -> 1. Paths with different numbers of
// points are made compatible by splitting the longest segments of the shorter one (the curve itself does not change);
// points are then paired in order. Colours, stroke width and handles are interpolated too.
VectorShape morphShape(const VectorShape& a, const VectorShape& b, double t);
// Shapes are paired by position in the list. Shapes without a partner appear or disappear at the halfway point.
ShapeList morphShapes(const ShapeList& a, const ShapeList& b, double t);

}  // namespace mynd
