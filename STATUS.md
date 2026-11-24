# Development status

**Current phase:** 2 — Traditional animation (implemented and tested, see below); Phase 1 complete before it
**Build:** Linux x86-64, GCC 13.3, Qt 6.4.2, CMake 3.28 — builds with no warnings.
**Tests:** 11 CTest suites, 170 test cases, all passing (also clean under AddressSanitizer + UBSan). UI behaviour is verified with simulated mouse/keyboard input in an offscreen Qt session, not by a person on Windows/macOS yet.
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
the other four workspaces (they are added in the phase that introduces their panels) ·
everything in Phases 3–11 (property animation, graph editor, 3D, effects, audio, video export, packaging).

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

## Phase 2 — Traditional animation
**Acceptance:** *a user can create a frame-by-frame animation, edit its drawings, change exposure timing, use onion
skinning, and play the animation.* Covered by `tests/test_ui_animation.cpp` (real `MainWindow`, synthesized mouse/keys),
`test_animation.cpp` (timing model), `test_playback.cpp` (clock), plus animation cases in the document, compositor and
project-file suites.

What exists:
- **Animation layers** (new layer type, Layer panel button / Animation menu). Each owns *drawings* (persistent id +
  name, raster bitmap each) and *exposures* (start, length, drawing, pose marker). One drawing can be exposed any
  number of times without copying pixels; editing it changes every frame that shows it. Frames with no exposure are
  empty; an *explicit blank exposure* is also supported.
- **Drawing**: all raster tools (brush, pencil, eraser, smudge, bucket, gradient, free transform) edit the drawing at
  the current frame; painting on an empty frame creates a drawing there (its own undo step). Undo/redo targets the
  drawing that was edited, not whatever frame the playhead is on.
- **Exposure sheet** (dock): frames × animation layers; shows drawing names, hold bars, blanks, pose colors. Typing a
  name exposes that drawing on that frame (new names create the drawing); empty clears; `x` = blank. Click = sets
  playhead + active layer; stays in sync with the timeline.
- **Timeline** (dock): ruler with frame numbers and seconds ticks, playhead, per-layer rows with exposure spans,
  scrub by dragging the ruler, click a cell, drag the end of an exposure to retime it, zoom (Ctrl+wheel), work area
  shading, transport buttons, frame/time/length/fps/speed controls. Frame numbers in the UI start at 1.
- **Timing commands** (Animation menu + context menus, all undoable): extend/shorten exposure, insert blank exposure,
  delete frame (ripple), make blank, repeat exposure, loop a frame range N times, substitute a drawing for a whole
  exposure or one frame, new drawing, duplicate drawing, "next frame with new drawing" (straight-ahead), previous/
  next drawing, key / breakdown / in-between markers (pose-to-pose). Edits affect the active layer only.
- **Onion skin**: N previous / N next drawings, opacity falloff, previous/next tint colors, count-held-frames option,
  active-layer-only option; persisted in user settings. Editor aid only — hidden during playback, never in exports.
- **Playback**: real-time clock using exact rational frame rates (23.976 / 29.97 / 59.94 supported, no drift), loop,
  work area, speed 25–200 %, frame skipping instead of slowing down; clicking the canvas pauses.
- **Project files**: format version 2 (version-1 files still open); drawings stored as PNGs under
  `assets/drawings/<layer>/<drawing>.png`; duration, frame rate, work area saved.
- **Animation workspace** preset (timeline bottom, exposure sheet right, onion skin + options left).
- Shortcuts: `,` `.` previous/next frame · `Home` `End` · `Alt+,` `Alt+.` previous/next drawing · `Shift+Enter` play/pause ·
  `Alt+Enter` stop · `Alt+O` onion skin · `Ctrl+Shift+N` new drawing · `Ctrl+Shift+D` duplicate drawing ·
  `Ctrl+Alt+N` next frame + new drawing · `Ctrl+=` / `Ctrl+-` extend/shorten · `Ins` blank exposure · `Ctrl+Del` delete frame ·
  `Alt+[` `Alt+]` work area in/out.

Known limitations (Phase 2):
- Drawings are **raster only**; there are no vector drawings inside animation layers yet.
- Playback composites on the CPU every frame. At 1920×1080 with many layers it may not reach the frame rate; frames
  are then skipped (counted in `Playback::skippedFrames`) — it does not slow the clock. No frame cache yet.
- No audio (so no audio sync), no video export (see Export below), no drag-and-drop of exposures,
  no cross-layer ripple edits, no lip-sync/camera/peg features.
- Onion skin applies to animation layers only.
- Changing the frame rate keeps frame numbers (it retimes the animation; it does not resample).
- The exposure-sheet toolbar overflows into a ">>" menu at narrow widths.
- Timeline/exposure sheet were exercised through automated input and screenshots of the offscreen renderer; they have
  not been tried by a person on Windows or macOS yet.

## Phase 2 completion: export and exposure-sheet selection
- **File > Export Animation… (Ctrl+Shift+E):** animated **GIF** (own encoder: median-cut palette per frame, LZW, loop count,
  optional 1-bit transparency, frame delays rounded with carried error so total length is exact) or a **PNG sequence**
  (`name_0001.png`…). Frame range defaults to the work area. Onion skin/overlays are never exported.
  Verified by decoding the GIF with an independent decoder in the tests, and by opening a generated file with ffmpeg/ImageMagick.
- **No video encoder** is built in (no MP4/MOV/WebM, no audio). Use the PNG sequence with ffmpeg. GIF is 256 colours per frame,
  1-bit transparency, and browsers play delays under 2 cs (above ~50 fps) slower than specified.
- **Exposure sheet** now supports selecting several frames of one layer (drag / Shift+click); the selection is shared with the
  timeline, so Copy / Cut / Paste / Delete work from either.

## Phase 3 — Property animation engine
Implemented:
- **Typed property model** (`Scene/Property.*`): persistent property ids, types (Float, Int, Bool, Vec2, Vec3, Vec4, Color,
  Rotation, Enum), defaults, validation ranges, sparse per-layer track map, keyframes (sorted, one per frame), serialisation.
  Only the 2D transform + opacity properties are *registered* so far: Anchor Point, Position, Scale (X/Y), Rotation, Opacity.
  The engine itself is generic: other types interpolate/validate and are covered by tests, but no UI or layer feature uses them yet.
- **Interpolation:** linear, hold, and temporal Bezier with per-key in/out type and handles (Ease In, Ease Out, Easy Ease presets);
  overshoot supported; Bool/Enum never blend; Int rounds.
- **Evaluation in the compositor:** every layer type, including groups and animation layers, is drawn through its matrix
  (anchor/position/scale/rotation about the anchor) and opacity at the current frame; identity transforms take the unchanged
  fast path. Used for the canvas, playback and exports alike.
- **Editing:** Properties dock (stopwatch, values, add/remove key, previous/next key, keep-proportions scale, Auto-Key);
  **Transform Layer tool (W)** (move, rotate with Shift snap, scale handles, draggable anchor that keeps the picture still);
  **Auto-Key** (Ctrl+Shift+K): off = editing a non-animated property changes its static value; on = it creates a keyframe at
  the playhead; an already animated property always keys.
- **Timeline integration:** expand a layer (arrow) to get a row per property with keyframe markers (diamond = linear, circle =
  eased, square = hold); click / Ctrl-click / marquee to select, drag to retime, Delete, Copy/Cut/Paste keyframes (to any layer),
  right-click presets, `F9` Easy Ease, `Ctrl+,` / `Ctrl+.` previous/next keyframe. Collapsed layers show a keyframe summary.
- **Undo:** every edit is a single undo step (a drag is one step); **save/load:** project format version 3 (v1/v2 open).
Limitations (honest list):
- Not animatable yet: Z / 3D properties, visibility, path geometry, stroke width, fill/stroke colour, text, effects, camera,
  light, material, audio. Their types exist in the engine but nothing is wired to them.
- No graph editor, speed graph, tangent dragging, or motion paths on the canvas (Phase 4). Easing can only be picked from presets.
- Keyframes sit on whole frames (the value can be evaluated between frames; keys cannot be placed there).
- Transform is not parented (no layer-to-layer parenting); a group transforms its children as a unit.
- The Transform Layer tool's box is the layer's opaque-pixel/shape bounds on the current frame; the rotate handle is the area
  just outside the box.
- Transformed layers are rendered through an intermediate buffer; heavy scenes may drop frames on CPU (no cache yet).
- Copy of a mix of properties from several layers merges frames per property; pasting always targets the active layer.

## Next tasks (Phase 4 — Graph Editor)
Value/speed graph, Bezier handle and tangent editing, spatial motion paths in the viewport, curve presets. Re-evaluate
renderer choice (Skia vs. QPainter) and add a frame cache before the GPU/3D work.

## Build / CI status
`.github/workflows/build.yml` builds and runs the tests on Linux, Windows (MSVC, Qt 6.8.3, vcpkg libzip) and
macOS. The Windows job also uploads a portable zip (windeployqt) after a headless `--screenshot` smoke test.
No installers (MSI/DMG/AppImage/DEB/RPM) exist.

## Timeline frame selection, copy/paste and panel layout
- Drag across timeline cells (or Shift+click) to select a range of frames on a layer; right-click offers Copy / Cut / Paste Frames.
- Paste overwrites starting at the current frame (playhead), preserves gaps, grows the composition if needed, and is one undo step.
  Paste makes independent drawing copies; Paste (Reuse Same Drawings), Ctrl+Shift+V, shares drawings when pasting on the same layer.
- Frame selection is only on the timeline (the exposure sheet has no multi-cell selection).
- Panels can be nested/stacked anywhere; each workspace (built-in or custom) remembers its arrangement when you switch away and on exit.
  "Reset Current Workspace" restores the factory layout. Verified headless only; not hand-tested on Windows.


## Selection tools merged
The Select tool (V) now covers Direct Selection and Transform Layer: on drawing/raster layers it transforms the whole layer (keyframeable); on vector/text layers it moves shapes, Alt+drag transforms the layer, Ctrl+drag edits points. The separate toolbar buttons and the A/W shortcuts were removed (internal tool classes remain). Verified by automated tests only; point editing via Ctrl is not yet exercised by a dedicated test.
