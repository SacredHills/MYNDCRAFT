#pragma once
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>

namespace mynd {

// Scanline flood fill on an ARGB32_Premultiplied image. `tolerance` is 0..255 per channel.
// Returns the bounding rect of changed pixels (empty if nothing changed).
QRect floodFill(QImage& img, const QPoint& seed, const QColor& color, int tolerance,
                bool contiguous = true);

}  // namespace mynd
