#pragma once
#include <QImage>
#include <QString>

#include "Scene3D/Render3D.h"

namespace mynd::gpu3d {

// OpenGL 3.3 version of renderScene(): the same camera, lights, materials, textures, culling, depth test and far-to-near sorted blending of
// see-through triangles, drawn on the GPU into an offscreen framebuffer (at the supersampled size, then scaled down exactly like the CPU
// renderer does). It is meant to be a drop-in for previews; the CPU renderer stays the reference (and is what exports use).
//
// render() returns a NULL image when it did not render, and the caller then uses the CPU renderer: no OpenGL, any GL error, an image bigger
// than the GPU allows, more than 16 lights, RenderOptions::distanceOut, a layer blend mode other than Normal, a texture with transparency on
// an otherwise opaque material, or a very large number of see-through triangles.
//
// All GL work happens on one thread that owns the context; render() may be called from any thread and waits for it.
bool initialize();                         // GUI thread, once, before the first render()
bool available();
QString rendererName();
void shutdown();
void setEnabled(bool on);
bool enabled();
QImage render(const Scene3D& s, const RenderCamera& cam, const QSize& size, const RenderOptions& opt);
int rendersDone();                         // diagnostics / tests: how many images the GPU produced so far

}  // namespace mynd::gpu3d
