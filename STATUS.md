# Development status

**Current phase:** 1 — Foundation and Drawing (complete for the scope below; see limitations)
**Build:** Linux x86-64, GCC 13.3, Qt 6.4.2, CMake 3.28 — builds with no warnings.
**Tests:** 5 CTest suites, 77 test cases, all passing (also clean under AddressSanitizer + UBSan).
**Other platforms:** Windows and macOS are built and tested in CI on every push (see bottom). The Windows portable build was run by hand by the project owner; the macOS build has not been run by a person. No installers.

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
- **Text:** text layers (point text or wrapped in a fixed-width box), inline editing on canvas with the
  Text tool (T): click empty canvas to create, click existing text to edit, Ctrl+Enter commits, Esc cancels,
  emptying a text layer deletes it. Font family, size, bold, italic, alignment, tracking, wrap width, color;
  options apply to the active text layer and are undoable (slider-style edits merge). Select tool moves text.
  Saved in the `.studio` file as text (not rasterized) and re-rendered on load. Text is not converted to
  outlines and not animatable yet.
- **Smudge brush (S):** drags existing pixels along the stroke on a raster layer (size, hardness, strength, spacing).
- **Free transform (Q):** scale (8 handles, Shift keeps proportions, Alt from center), rotate (drag just outside
  the box, Shift snaps 15°), move (drag inside) for selected vector shapes (baked into the path nodes) or the
  content of a raster layer (resampled once, so repeated transforms lose sharpness; each is one undo step).
- **Path editing on the outline:** with Direct Select, press and drag on the selected shape's outline to bend that segment directly (no point needed; works on straight edges too); double-click the outline to add a point (shape unchanged).
- **Image import:** File > Import Image… (Ctrl+Shift+I) or drag files onto the window; formats are whatever
  Qt's image plugins provide. Becomes a new raster layer above the active one, centered, scaled down if larger
  than the canvas. Dropping a `.studio` file opens it.
- **Workspaces:** Drawing and Illustration presets, user-saved named layouts (persisted).

## Partially implemented
- Tilt: captured from the tablet (`PointerEvent::xTilt/yTilt`) but not mapped to any brush dynamics.
- Brush presets / custom / texture brushes: not implemented; one settings block per tool.
- Settings: only autosave and grid size are exposed in Preferences; window geometry and workspace persist.
- Rulers/guides are hidden/inactive while the canvas is rotated.
- Layer compositing re-renders the dirty region on the CPU via `QPainter` (no GPU, no render cache).

## Not implemented (Phase 1 scope items deliberately left out)
Colour-mixing (wet) brushes · boolean path operations · text-to-outlines · SVG import (needs QtSvg, not
available in the current dev environment) · custom shortcuts · multi-document tabs ·
the other five workspaces (they are added in the phase that introduces their panels) ·
everything in Phases 2–11 (animation, timeline, graph editor, 3D, effects, audio, video export, packaging).

## Known bugs / limitations
- Max canvas 16384 px per side on load; New dialog refuses > 100 megapixels. Each raster layer is a
  full-canvas image, so memory is `layers × w × h × 4` bytes.
- Gradient/paint-bucket on raster has no gap-closing and no anti-aliased edge expansion.
- Group layers are isolated only when opacity < 1 or blend ≠ Normal (pass-through otherwise).
- Bucket fill samples the active layer only (no "sample merged").
- Free transform on raster is destructive resampling; it works on the layer's whole canvas and does not
  extend beyond the canvas (content moved off-canvas is cropped).
- Raster free-transform bounds ignore partially transparent noise only by alpha > 0; a faint stray pixel enlarges the box.
- Text rendering depends on installed fonts; a project opened on a machine without the font falls back to a
  substitute (the family name is kept in the file).
- The "fps" setting is stored and saved but nothing consumes it yet.

## Next tasks (Phase 2 — Traditional animation)
Drawing/exposure data model separate from property keyframes; exposure sheet panel; timeline panel
with playback clock; onion skin renderer; drawing substitution; then re-evaluate renderer choice
(Skia vs. QPainter) before the GPU/3D work in Phase 6.

## Build / CI status
`.github/workflows/build.yml` builds and runs the tests on Linux, Windows (MSVC, Qt 6.8.3, vcpkg libzip) and
macOS. The Windows job also uploads a portable zip (windeployqt) after a headless `--screenshot` smoke test.
No installers (MSI/DMG/AppImage/DEB/RPM) exist.
