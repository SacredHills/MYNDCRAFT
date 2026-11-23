# Architecture and decisions

## Layering

```
src/Core      settings, undo commands, project IO (.studio), autosave      ┐ libmyndcore
src/Scene     Document, Layer tree, VectorShape (pure data + invariants)    │ (no QtWidgets)
src/Drawing   brush engine, flood fill (operate on QImage)                  │
src/Graphics  Compositor (document -> pixels)                               ┘
src/UI        EditorSession, Tools, CanvasView, Panels, Workspaces, MainWindow  (libmyndui, Qt Widgets)
tests/        QtTest suites, run headless with QT_QPA_PLATFORM=offscreen
```

Dependencies point downwards only: the engine library never includes widgets, so the render,
export and (later) command-line render paths can run without a UI.

## Key decisions

1. **Document vs. session.** `Document` is everything that is saved. `EditorSession` is transient
   (tool, colors, brush settings, selection, active layer). Nothing in `Document` knows about tools or panels.
2. **Persistent identity.** Layers and vector shapes carry UUIDs that survive save/load, undo and
   duplication (duplicates get fresh ids). Commands reference objects by id, never by pointer, so
   they stay valid when the tree is rebuilt.
3. **Undo = commands that mutate the document and are replayable.** Raster edits are applied live
   while drawing (brush engine keeps the pre-stroke pixels) and recorded afterwards as a dirty-rect
   patch (`RasterPatchCommand`) — memory proportional to what changed, not to canvas size.
   Vector edits snapshot the layer's shape list (cheap: paths are small).
4. **Moves are two-phase but one notification.** Moving a layer removes then re-inserts it; the
   intermediate removal is silent so observers (active layer, panels) never see a phantom delete.
5. **Time.** `FrameRate` is a rational (24000/1001 etc.); timestamps are computed from integer
   frame numbers, never accumulated floats. Phase 2+ will build the composition clock on this.
6. **Project container.** ZIP with `manifest.json` (format name + integer schema version) and
   `composition.json`; raster layers are PNG assets referenced by id. Loading validates before
   trusting anything (size limits, nesting depth, unique ids). `ProjectIO::migrate` is the single
   place schema upgrades will be added; there are no upgrades yet because v1 is the first schema.
7. **Qt Widgets (not QML) for Phase 1.** `QMainWindow` gives dockable, tabbable, saveable panel
   layouts that meet the workspace requirements with the least risk. Panels talk only to
   `EditorSession`/`Document`, so a QML front end could be introduced for specific panels later.

## Renderer evaluation (honest status)

The spec asks for Skia (2D) and Filament (3D) to be evaluated before commitment. **Neither has been
built or benchmarked here**; the notes below are from their documentation and general knowledge and
must be re-checked in the phase that needs them.

- **Phase 1 uses Qt's `QPainter` raster engine** inside `Compositor`. It is stable, cross-platform,
  needs no extra dependency and is fast enough for the current CPU-composited workload.
  The compositor takes a document and a region and returns an image, so a GPU/Skia backend can
  replace it behind the same call.
- **Skia (BSD-3):** attractive for GPU-accelerated vector/raster compositing, blend modes and
  effects. Costs: large build, no stable binary releases, non-trivial Qt integration (own GPU
  context/texture sharing). Decision deferred to the start of Phase 9 (effects/compositing), when
  CPU compositing performance can be measured against real projects.
- **Filament (Apache-2.0):** PBR renderer with glTF loading; supports Vulkan/OpenGL/Metal; no
  built-in FBX/OBJ import. Rendering into a Qt window and compositing with 2D content needs a
  shared-texture or readback path — the main technical risk for Phase 8. Decision at Phase 6.
- **Assimp (BSD-3):** broad format coverage; FBX skeletal/animation import is the usual source of
  edge-case bugs. Plan: GLB/glTF first (Filament's gltfio), Assimp for FBX/OBJ.

## Risks

| Risk | Phase | Mitigation |
|---|---|---|
| Memory: full-canvas raster layer per layer | now | tile-based layer storage before long animations (Phase 2/10) |
| Unified 2D/3D compositing (shared GPU textures through Qt) | 8 | prototype Filament-in-Qt early in Phase 6 |
| Traditional drawings × property keyframes × tweens on one timeline | 2–5 | keep exposure data separate from property tracks; both evaluated by one clock |
| FFmpeg licensing | 11 | decide codec set + build config before integrating |
| Windows/macOS/Linux packaging untested | 11 | CI matrix from Phase 2 |
