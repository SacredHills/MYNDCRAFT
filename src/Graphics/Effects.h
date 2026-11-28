#pragma once
#include <QImage>
#include <QPoint>
#include <QSize>

#include "Scene/LayerFx.h"

namespace mynd::fx {

// Gaussian-like blur (three box passes) of an ARGB32_Premultiplied or Alpha8 image, in place. `radius` is in pixels (about 3 sigma);
// pixels outside the image count as transparent.
void blur(QImage& img, double radius);

// Extra pixels around the layer a chain of effects needs so blurs/shadows/glows are not cut off.
int effectMargin(const QVector<Effect>& effects);

// Runs the enabled effects in order on an ARGB32_Premultiplied image.
void applyEffects(QImage& img, const QVector<Effect>& effects);

// Coverage (Alpha8, 255 = fully inside) of the masks over a `size` area whose top-left is layer-space `origin`. Masks combine in list order.
QImage maskCoverage(const QVector<Mask>& masks, const QSize& size, const QPoint& origin);
// img (ARGB32_Premultiplied) times coverage (same size).
void applyCoverage(QImage& img, const QImage& coverage);
// img times the matte's alpha or luminance (matte: ARGB32_Premultiplied, same size), optionally inverted.
void applyMatte(QImage& img, const QImage& matte, MatteMode mode);

}  // namespace mynd::fx
