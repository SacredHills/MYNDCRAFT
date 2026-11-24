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

## Phase 4 — Graph Editor (implemented, tested headlessly; visual feel not yet confirmed on Windows)
- **Graph Editor** tab next to the Timeline (same dock): pick an animated property; value graph draws each component
  as a curve; keys can be selected (shared with the timeline), dragged in time (integer frames, between neighbours)
  and value; Bezier handles of selected keys are draggable (shared handle, shown on the component that changes most);
  Linear/Hold/Ease buttons; Fit, wheel zoom (Shift = vertical), middle-drag pan, ruler click scrubs. All edits are undoable.
- **Speed graph** (read-out only): speed in units per frame; editing is done in the value graph.
- **Motion path** on the canvas (Select tool): Position keyframes are drawn as a path with a dot per frame; drag keys
  to move them; selected keys show spatial tangent handles (Alt = break symmetry). Spatial tangents are new Keyframe
  data (`spatialIn/Out`, saved in .studio) and curve the Position path; timing still comes from the temporal ease.
- Limitations: spatial tangents only for Vec2 properties (Position is the one that is evaluated with them); the temporal
  handle is shared across a Vec2's components (no separate dimensions); speed is not parameterised by arc length;
  no box-select or multi-key handle edits in the graph; speed graph cannot be edited.

## Phase 5 — Symbols (implemented, headless-tested; feel not yet confirmed on Windows)
- **Library + instances:** a symbol is a named layer tree with its own length (Library tab next to Graph Editor, Symbol menu).
  F8 / "Convert Layer to Symbol" moves the active layer (or group) into a new symbol and leaves an instance in its place.
  Instances are normal layers (transform/opacity keyframes, Auto-Key, graph editor all work) sharing one definition.
- **Graphic** symbols follow the timeline they sit in (Loop / Play Once / Single Frame, first frame); **Movie Clip** symbols
  loop on their own from frame 0. Time is evaluated deterministically from the parent frame (no free-running clock), so
  export and scrubbing agree.
- **Nested symbols** work; cycles (a symbol containing itself, directly or through others) are refused at creation and
  rejected/validated on load. Rendering has a depth guard.
- **Symbol editing mode:** double-click an instance with Select, or Edit Symbol (Ctrl+Shift+Down); breadcrumb + Exit button
  (Ctrl+Shift+Up). Inside, the canvas, layers, timeline and length are the symbol's. Entering/leaving is part of undo history
  so undo always runs in the right context (side effect: entering a symbol marks the project modified).
- **Saved** in format version 4 (`symbols` in composition.json), also while a symbol is open (the main scene is written).
- Limitations: no "symbol editing in place" (the rest of the scene is hidden while inside); Movie Clips do not offer
  independent play/stop; no button-style symbols; instance cannot be swapped for another
  symbol yet; deleting a used symbol is refused rather than removing its instances.

## Phase 5 — Tweens and assets (implemented, headless-tested; feel not yet confirmed on Windows)
- **Motion / Classic tweens** (Animation menu → Create Tween): presets that write ordinary transform/opacity keyframes
  between two frames (ease -100..100, rotate none/CW/CCW + turns). They are not a separate tween object: edit the result
  in the timeline, graph editor or motion path; "Remove Tween" deletes the keys in the span. Motion and Classic currently
  produce the same keys.
- **Shape tweens** (`shape.morph` property): vector layer morphs between its start shapes and an end shape (toggle
  "Edit End Shape"). Shapes pair by index; paths with different point counts are padded by splitting the longest
  segments; unpaired extra shapes pop in/out at the halfway point; gradients switch at halfway; no shape hints.
- **Reusable assets:** Symbol → "Export Symbol as Asset…" writes the symbol plus the symbols it uses to a .studio file;
  "Import Symbols from Project…" merges all symbols of any .studio into the library with new ids and unique names
  (undoable). Artwork is re-centred if the source canvas size differs; it is not scaled.
- Independent symbol timelines: Movie Clips loop on their own, Graphics follow the parent (see above).
- No visual tween span in the timeline yet.

## Phase 6 — 3D scene editor (implemented, headless-tested; not yet hand-tested on Windows)
- **3D scene layers** (3D menu → New 3D Scene Layer, or just import a model / add a primitive). A scene has a node hierarchy
  (groups, meshes, lights, cameras), materials, textures and a scene camera. The layer is rendered through the scene camera
  into the composition like any other layer (transform, opacity, blend mode, undo, save/load all work). Project format v5.
- **Renderer:** our own CPU renderer (no GPU, identical everywhere, usable headless): perspective camera, z-buffer, per-pixel
  Blinn-Phong lighting with metallic/roughness-style materials, bilinear base-colour textures, directional/point/spot lights
  (+ ambient), 2x anti-aliasing (automatic below ~1.2 MP, always for export), render cache. **Not** supported: shadows,
  reflections/environment maps, normal/roughness/emissive maps, transparency sorting, skinning. Speed is software speed:
  fine for modest models, slow for very heavy ones.
- **Importers:** glTF 2.0 / GLB (hierarchy, TRS or matrix, PBR base colour/metal/rough factors, base-colour textures,
  cameras, KHR lights; Draco/meshopt compressed files are refused with a message), Wavefront OBJ + MTL (polygons, negative
  indices, per-object nodes, Kd/Ke/Ns/d, map_Kd), binary FBX 7.1-7.7 (geometry with normals/UVs/materials per polygon, hierarchy,
  Lcl/Pre rotation, scale, diffuse colour + texture; Z-up files are rotated). **FBX limits:** ASCII FBX, cameras, lights,
  skeletons/skins and animation are not imported; units are kept as in the file (FBX is usually centimetres, rescale the root
  group). glTF animation/skins are ignored with a warning (Phase 7). Every importer range-checks its input and was fuzzed with
  random corruption (also under ASan/UBSan). The importers have only been tested with files we generate ourselves
  and the formats' documentation - not with a large collection of real-world exports.
- **3D View** (dock, "3D" workspace): orbit (Alt+drag / right drag), pan (Shift+Alt+drag), wheel zoom, F to frame, click to
  select, Move / Rotate / Scale gizmos (W/E/R; Ctrl snaps), a "Camera View" that shows exactly what the composition gets.
  Move/Rotate use world axes, Scale uses the node's own axes (centre handle = uniform).
- **3D Scene** (dock): hierarchy tree (visibility checkbox, drag onto a node to re-parent, context menu), Import Model, Add
  (cube, sphere, plane, cylinder, point/directional/spot light, camera, group), and an inspector: transform, material
  (colour, roughness, metallic, emissive, double-sided, base texture), light, camera, scene (ambient, background, scene camera).
- Importing a model into a scene with no light/camera adds a directional light and a camera framing the model.
- Not done in Phase 6 (by design): modelling tools, animation of 3D properties (Phase 7), instancing/duplicated meshes share
  geometry by copy, no mesh editing, no shadow casting.

## Phase 7 — 3D animation (implemented, headless-tested; not yet hand-tested on Windows)
- **Keyframing 3D properties on the normal timeline/graph editor.** 3D properties are stored as ordinary property tracks of the 3D layer
  (ids `3d:n:<node>:<what>`, `3d:m:<material>:<what>`; see `src/Scene3D/Anim3D.h`), so the timeline rows, graph editor, key selection/move/delete,
  Easy Ease presets and undo work the same as for 2D. Animatable: node position / rotation / scale, camera field of view, light intensity /
  colour / range / spot angle, material base colour / emissive / roughness / metallic.
- **Inspector and viewport.** Every animatable row in the 3D panel has a stopwatch and a key button. The viewport and inspector show the scene
  evaluated at the current frame. Editing an animated property (typing or dragging a gizmo) writes a key at the current frame in one undo
  step; with Auto-Key on, editing any animatable property keys it. Unanimated properties keep changing the stored scene as before.
- **Rendering.** The compositor evaluates the scene at the frame (cached by scene revision + evaluated values). Export uses the same path.
- **Imported animation.** glTF/GLB node translation/rotation/scale animations become clips (times converted to composition frames at import
  time). Clips can be placed on the timeline with start frame, speed, offset and loop/hold, enabled/disabled, from the "Animation Clips" group in
  the 3D panel. The first clip is placed looping on import. Keyframes you set on a property override the clip on that property.
- **Skeletal animation.** glTF skins (up to 4 joints per vertex) are imported and drawn deformed by the animated joint nodes (CPU skinning).
- **Persistence.** Keyframes, clips, placements and skins are saved in the project (3D scene JSON, mesh blobs carry joints/weights). Keyframes of
  deleted nodes are dropped on save; duplicated layers renumber their keyframes, clips and skins.
- **Tests.** `test_anim3d` (15), additions to `test_ui_3d` and the import fuzz test; also clean under ASAN/UBSAN.
- **Limitations (honest list).**
  - Clip time is stored in composition frames at import: changing the project frame rate later changes the clip's real-time speed.
  - Clips are not drawn as blocks on the timeline; manage them in the 3D panel. No blending/crossfade between clips; placements are applied in list order.
  - glTF only: FBX and OBJ import no animation or skins (FBX animation/skeleton is not implemented). Morph targets (blend shapes) are ignored.
  - Cubic-spline channels are imported as linear keys; rotation is converted to Euler keys (extra keys are inserted on long turns) so
    imported rotations are an approximation of the original quaternion interpolation, and graph editor shows Euler angles.
  - Skinning ignores the skinned node's own transform (as glTF specifies); picking uses the deformed pose but the selection gizmo/bounds use the node's own transform.
  - Duplicating a single node does not copy its keyframes (duplicating the whole layer does).
  - The scene camera, ambient/background and light type are not animatable (only the properties listed above).

## 2D layers in 3D space ("3D Layer" cards) (implemented, headless-tested; not yet hand-tested on Windows)
- **How to use.** Select any 2D layer (raster, vector, text, animation, symbol instance or group) and choose *Layer ▸ 3D Layer* (or *3D ▸ 3D Layer*).
  The layer becomes a **card**: a flat plane inside a 3D scene layer that is placed directly above it (an existing scene layer in the same
  group is reused). The card gets a normal 3D transform (position, rotation, scale, all keyframeable like any 3D node), is depth-correct against
  the scene's 3D objects and other cards, and is seen through the scene camera. A new scene gets a camera at the distance where a card at the
  origin looks exactly like the flat layer. Unchecking the action (or "Make Layer 2D Again" in the panel) restores the plain layer.
- **What the card shows.** The layer's current content at the current frame (vector shapes, text, drawings, animation layer frames, symbols),
  rendered at canvas resolution; the layer's opacity (and its animated opacity) becomes the card's transparency. By default the card is flat
  (unlit, so it looks exactly like the 2D layer); "Lit by the scene's lights" switches it to normal shading.
- **Rendering order.** The source layer is no longer drawn by itself; only the card shows it. See-through meshes (cards and translucent materials) are drawn
  after opaque ones and sorted far-to-near per object.
- **Tests.** `test_layer3d` (8 core tests) and an end-to-end UI test; clean under ASAN/UBSAN.
- **Limitations.** The layer's own 2D transform (position/rotation/scale tracks), blend mode and masks are not applied to the card: use the
  card's 3D transform instead (opacity is applied). The card texture is the canvas size, so a card scaled up in 3D softens (it is not re-rendered at
  higher resolution) and edges are bilinear-filtered. Translucent surfaces are sorted per object, not per triangle, so crossing/intersecting
  translucent cards can blend in the wrong order. Cards are re-rendered whenever the scene is redrawn (a scene with many large cards will be slow
  on this CPU renderer). If the canvas size changes after a card was made, the card keeps its old shape. Cards of layers inside symbols are untested.
  Text/vector layers with effects that depend on the 2D transform will not show them. Not hand-tested on Windows.

## Layer panel: context menu, clipboard, 3D column, scrubbable values (implemented, headless-tested; not yet hand-tested on Windows)
- Right-click a layer in the Layers panel: copy / cut / paste, duplicate, rename (F2), delete, group, order, 3D Layer and its settings. Works for every layer type; paste goes above the active layer, with new ids (groups keep children). Undoable.
- "3D" checkbox column right of Lock: turns a 2D layer into a 3D card and back (not shown for 3D scene layers). Stays in sync when toggled from the inspector.
- Fix: unchecking a 3D layer crashed (use-after-free on a reference into a deleted card node). Ids are now passed by value; regression test run under ASAN.
- Transform/inspector number fields can be dragged horizontally (Shift fine, Ctrl coarse; click to type). Covers 3D inspector fields and 2D property fields. Reworked after a report that dragging did nothing: it now also works on a field that already has focus and when the press lands on the spin box border; tests use real QTest mouse press/move/release. The scrub cursor (horizontal arrows) shows on hover and while dragging; Ctrl+Z / Ctrl+Y act on the document (undo the scrub) even while the field is focused, unless there is typed text not yet applied.
- Not done: context menu on timeline layer rows.
- Import size: the 3D model importer refused files over 1 GB; now there is only a 64 GB sanity limit. Files (and glTF external .bin files) are memory-mapped instead of copied, and GLB chunks are read in place. A single-file .glb cannot exceed 4 GB by the glTF format itself (32-bit chunk sizes); bigger models need .gltf + external .bin. The decoded model (meshes, decoded textures in RAM, CPU renderer) still has to fit in memory, so a 6 GB model may still fail or be very slow; nothing has been tried above a sparse dummy file. The UI is busy while importing (no progress bar / background thread yet). Images: Qt's 256 MB decode limit raised to 2 GB. Draco/meshopt-compressed glTF still unsupported. Only tested with a sparse dummy file, not a real >1 GB model.
- Freezes: the 2-minute autosave encoded every layer as PNG on the UI thread (about 650 ms for 10 full-HD layers here; scales with layers/frames/3D textures), which stalls the app without much CPU use. Autosave now snapshots on the UI thread (~0 ms) and encodes/writes on a worker thread; saving/closing waits for a running write. Manual Save is still synchronous (so a big project still pauses while you save). This is my best identified cause; other possible stalls (very large imports on the UI thread, first playback pass) are not fixed. Not measured on your machine.
- View > "Canvas in Separate Window": moves the canvas into its own floating, movable window (drag it anywhere, other screen, or dock it); uncheck to put it back in the middle. Workspaces do not manage it (not remembered across restarts). Making the canvas a normal dock in every workspace was tried and dropped: the dock layout could not be sized reliably, which broke canvas tests.
- Check boxes in the Layers panel and the 3D hierarchy are drawn light (light box, dark tick).
- Playback: frames shown while playing are kept in a RAM cache (up to ~768 MB), so the second and later loops show them without re-rendering; any edit drops the cache. The FIRST pass still renders every frame live, so a heavy comp can still drop frames then (measured here: ~14 ms/frame compositing for 8 full-HD layers; your machine may differ). No background pre-render yet. Not measured on Windows.
- 3D Scene panel node checkbox: the list is no longer rebuilt while its own check event is being handled. I could not reproduce a crash from the node checkboxes in tests (with or without this change), so if it still crashes on the new build please tell me exactly what was clicked.
- Fix: workspaces came back different depending on the previous one (Qt leaves panels a saved layout doesn't mention where they were, and hidden panels weren't remembered). Every restore now starts from all panels undocked, and hidden panels are stored with the layout (old saved layouts still load). Test cycles all workspaces and compares panel visibility/placement. Not hand-tested on Windows.

## Next tasks
1. Phase 8: unified 2D/3D composition (awaiting go-ahead).
2. Visual tween spans in the timeline, swapping an instance's symbol, per-instance Movie Clip play/stop; FBX animation; clip blocks on the timeline.
3. The 3D renderer is a CPU software renderer; a GPU backend is future work.

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
