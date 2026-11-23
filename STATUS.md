# Development status

**Current phase:** 1 — Foundation and Drawing (complete for the scope below; see limitations)
**Build:** Linux x86-64, GCC 13.3, Qt 6.4.2, CMake 3.28 — builds with no warnings.
**Tests:** 5 CTest suites, 60 test cases, all passing (also clean under AddressSanitizer + UBSan).
**Not built or tested:** Windows, macOS, installers, packaging, CI workflow itself (see bottom).

## Phase 1 acceptance check
> A user can create a project, draw with raster and vector tools, organize layers, save the project,
> close the application, reopen it, and continue editing.

Covered by `tests/test_ui_workflow.cpp` (drives the real `MainWindow` with synthesized mouse and
tablet events): draw → undo → save → new project → reopen → composite is pixel-identical and vector
shapes are still editable. Interactive use on a real display with a real tablet has **not** been tried.

## Completed
- **Document model:** composition (size, rational frame rate, background, grid, guides), layer tree
  (raster / vector / group, nesting), persistent UUIDs for layers and shapes, rational `FrameRate`
  with drift-free frame→time conversion (foundation for the shared composition clock).
- **Undo/redo:** command-based; layer add/delete/duplicate/move/group/ungroup, property edits
  (slider drags merge into one step), raster patches (dirty-rect before/after), vector edits.
- **Project files:** `.studio` ZIP (manifest + composition JSON + PNG per raster layer), versioned
  schema, atomic write, `.bak` of the previous save, validation, size limits, missing-asset warnings,
  clear errors for corrupt / foreign / newer-version files.
- **Autosave & crash recovery:** periodic autosave copy, recovery prompt at startup, removed on clean exit/save.
- **Raster:** brush / pencil / eraser (size, opacity cap per stroke, flow, hardness, spacing,
  stabilizer, pressure→size, pressure→opacity, aliased pencil), flood-fill bucket with tolerance,
  raster gradient fill, eyedropper. Tablet pressure is read from `QTabletEvent` (tested with synthetic events).
- **Vector:** editable Bézier paths; pen, line, rectangle, ellipse, polygon tools; selection,
  marquee, nudge, delete; direct selection (move anchors/handles, smooth/corner toggle, delete point);
  solid + linear-gradient fills, stroke color/width/cap/join; shape grouping; z-order commands.
- **Layers:** visibility, lock, opacity, 8 blend modes (Normal, Multiply, Screen, Overlay, Darken,
  Lighten, Add, Difference), isolated group compositing, rename, drag-and-drop reorder/re-parent.
- **Canvas:** zoom (cursor-anchored), pan (hand / Space / middle mouse), rotate, fit, 100 %,
  pixel-level zoom (nearest-neighbour ≥ 200 %), grid, rulers, guides, snapping, brush cursor, incremental redraw.
- **UI:** dark theme, dockable/floating/tabbable panels (Tool Options, Color & Style, Layers, History),
  tool bar, menus (File, Edit, View, Object, Layer, Window, Help), status bar, New/Open/Save/Save As/
  recent files/Export PNG-JPEG-BMP, Preferences (autosave, grid size).
- **Workspaces:** Drawing and Illustration presets, user-saved named layouts (persisted).

## Partially implemented
- Tilt: captured from the tablet (`PointerEvent::xTilt/yTilt`) but not mapped to any brush dynamics.
- Brush presets / custom / texture brushes: not implemented; one settings block per tool.
- Settings: only autosave and grid size are exposed in Preferences; window geometry and workspace persist.
- Rulers/guides are hidden/inactive while the canvas is rotated.
- Layer compositing re-renders the dirty region on the CPU via `QPainter` (no GPU, no render cache).

## Not implemented (Phase 1 scope items deliberately left out)
Smudge/blending brushes · text tool and text layers · free-transform tool · boolean path operations ·
adding points to an existing path · image import · custom shortcuts · multi-document tabs ·
the other five workspaces (they are added in the phase that introduces their panels) ·
everything in Phases 2–11 (animation, timeline, graph editor, 3D, effects, audio, video export, packaging).

## Known bugs / limitations
- Max canvas 16384 px per side on load; New dialog refuses > 100 megapixels. Each raster layer is a
  full-canvas image, so memory is `layers × w × h × 4` bytes.
- Gradient/paint-bucket on raster has no gap-closing and no anti-aliased edge expansion.
- Group layers are isolated only when opacity < 1 or blend ≠ Normal (pass-through otherwise).
- Bucket fill samples the active layer only (no "sample merged").
- The "fps" setting is stored and saved but nothing consumes it yet.

## Next tasks (Phase 2 — Traditional animation)
Drawing/exposure data model separate from property keyframes; exposure sheet panel; timeline panel
with playback clock; onion skin renderer; drawing substitution; then re-evaluate renderer choice
(Skia vs. QPainter) before the GPU/3D work in Phase 6.

## Build / CI status
`.github/workflows/build.yml` (Ubuntu + macOS) is written but **has never run**. No Windows job exists.
No packaging (AppImage/DEB/RPM/DMG/installer) exists.
