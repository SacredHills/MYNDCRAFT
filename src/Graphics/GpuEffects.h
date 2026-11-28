#pragma once
#include <QImage>
#include <QString>

#include "Scene/LayerFx.h"

namespace mynd::fx::gpu {

// Optional OpenGL (3.3 core) path for layer effects. Everything here is a speed-up of fx::applyEffects only: the result matches the CPU path
// to within a couple of 8-bit levels, and any problem (no OpenGL, a context or shader error, an image larger than the GPU allows) makes
// applyEffects() return false so the caller simply runs the CPU path. Off by default (Settings > GPU effects).
//
// OpenGL work happens on one dedicated thread that owns the context; applyEffects() may be called from any thread and waits for it.

// Creates the context (call once from the GUI thread, before the first use). Returns whether a usable GPU path exists.
bool initialize();
bool available();
QString rendererName();                    // "NVIDIA ...", "llvmpipe (software)", ... or empty when unavailable
void shutdown();

void setEnabled(bool on);                  // runtime switch (Settings)
bool enabled();

// Runs the enabled effects, in order, on an ARGB32_Premultiplied image in place. false = not done, image untouched.
bool applyEffects(QImage& img, const QVector<Effect>& effects);

}  // namespace mynd::fx::gpu
