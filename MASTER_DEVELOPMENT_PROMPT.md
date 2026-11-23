# Master Development Prompt: Professional 2D Animation, Motion Graphics, Compositing & 3D Scene Creation Software

## 1. Your Role

Act as a senior desktop application architect, C++ graphics programmer, 2D animation systems engineer, 3D graphics engineer, rendering specialist, and professional creative-software UI/UX designer.

Your task is to design and build a professional, cross-platform desktop application for drawing, illustration, traditional 2D animation, motion graphics, compositing, and 3D scene creation.

The application should combine the strongest workflow concepts of:

- Adobe Animate — vector drawing, symbols, timeline animation, motion tweens, and shape tweens.
- Clip Studio Paint — professional raster drawing, painting, brushes, and illustration tools.
- Toon Boom Harmony — traditional frame-by-frame animation, drawing substitutions, exposure sheets, onion skinning, and animation workflows.
- Adobe After Effects — property-based animation, keyframes, Graph Editor, compositing, effects, and motion graphics.
- A professional 3D scene editor — imported 3D models, cameras, lighting, materials, animation, and integration with 2D scenes.

The goal is to create a serious creative application with real functionality, not a prototype, mockup, or collection of nonfunctional interface elements.

**Most important requirement:** All drawing, animation, compositing, and 3D systems must work together in one unified project and composition architecture.

Do not sacrifice traditional animation workflows in favor of property-based animation. Do not sacrifice property-based animation in favor of traditional animation. All systems must coexist.

---

# 2. Core Application Goals

Build a professional desktop application that supports:

1. Professional raster drawing and painting.
2. Vector drawing and editing.
3. Traditional frame-by-frame 2D animation.
4. Animate-style timeline animation and tweening.
5. After Effects-style property-based keyframe animation.
6. Professional Graph Editor with value and speed graphs.
7. 2D compositing and effects.
8. A complete 3D scene editor for imported 3D assets.
9. 3D object, camera, lighting, material, and animation controls.
10. 2D and 3D elements inside the same composition.
11. A unified timeline and property animation system.
12. Audio tracks and synchronization.
13. Professional playback and rendering.
14. Project saving, recovery, import, and export.
15. Cross-platform Windows, macOS, and Linux builds.

The software should feel like a cohesive professional application, not several unrelated tools sharing a window.

---

# 3. Technology Stack and Architecture

Use a modern, maintainable architecture.

### Primary technology recommendations

- Language: C++20 or newer.
- Desktop framework: Qt 6.
- UI: Qt Quick/QML where appropriate, with C++ backend systems.
- Build system: CMake.
- 2D rendering: Evaluate Skia or another suitable high-performance 2D renderer.
- 3D rendering: Evaluate Google Filament or another suitable real-time 3D rendering engine.
- 3D asset importing: Assimp or equivalent.
- Video and audio processing: FFmpeg.
- Image processing: Suitable GPU-accelerated and CPU fallback libraries.
- Testing: CTest and an appropriate C++ testing framework.
- Version control: Git.

Select stable, actively maintained dependencies with suitable licenses for commercial distribution.

Before committing to a library, investigate its capabilities, licensing, platform support, maintenance status, and integration requirements.

Do not assume a suggested library can provide every feature automatically. Build the missing application-level systems yourself.

### Architecture requirements

Create separate, modular systems for:

- Application and workspace management.
- Document and project management.
- Scene graph.
- 2D rendering.
- 3D rendering.
- 2D drawing and painting.
- Vector graphics.
- Traditional animation.
- Timeline and exposure sheets.
- Property animation.
- Tween animation.
- Graph Editor.
- Compositing.
- Effects.
- Audio.
- Asset management.
- Import and export.
- Undo and redo.
- Autosave and recovery.
- Playback and rendering.

Use a shared document model and persistent object identifiers.

Avoid tightly coupling the UI to the rendering or animation engines.

The application must remain extensible so that future features can be added without rewriting the core architecture.

---

# 4. Professional User Interface

Create a polished, professional dark interface.

The interface should be inspired by professional creative software, with a clean layout and customizable workspaces.

### Main interface layout

- Top: Main menu, application controls, workspace selector.
- Left: Drawing and object tools.
- Center: Main canvas or viewport.
- Right: Properties, effects, colors, materials, assets, and inspector panels.
- Bottom: Timeline, animation tracks, exposure sheets, and audio tracks.

All panels must be resizable, dockable, collapsible, and rearrangeable.

Allow users to save custom workspace layouts.

### Main menus

Include:

- File
- Edit
- View
- Object
- Layer
- Animation
- 3D
- Effects
- Window
- Help

### Workspaces

Provide dedicated workspaces:

1. Drawing
2. Illustration
3. Traditional Animation
4. Motion Graphics
5. Compositing
6. 3D Scene
7. General Animation

Changing workspaces should reorganize panels and tools without changing or restricting the underlying project capabilities.

Users must be able to access 2D and 3D content from the same project regardless of the active workspace.

---

# 5. Drawing and Illustration System

Build a professional drawing and painting environment.

### Drawing tools

Include:

- Selection tool.
- Direct selection tool.
- Free transform tool.
- Brush.
- Pencil.
- Eraser.
- Pen tool.
- Bézier path tool.
- Line tool.
- Rectangle tool.
- Ellipse tool.
- Polygon tool.
- Paint bucket.
- Gradient tool.
- Eyedropper.
- Text tool.
- Hand/pan tool.
- Zoom tool.

### Raster drawing

Support:

- Multiple raster layers.
- High-resolution canvases.
- Adjustable brush size.
- Brush opacity.
- Brush flow.
- Brush hardness.
- Brush spacing.
- Stabilization.
- Pressure sensitivity.
- Tilt sensitivity where supported.
- Custom brushes.
- Brush presets.
- Texture brushes.
- Smudge tools.
- Blending brushes.
- Eraser presets.
- Brush blending modes.

Support common graphics tablets and styluses, including pressure-sensitive input.

### Vector drawing

Support:

- Editable vector paths.
- Bézier curves.
- Anchor points.
- Stroke width.
- Stroke color.
- Fill color.
- Gradients.
- Path editing.
- Shape editing.
- Stroke and fill properties.
- Boolean path operations where practical.
- Vector object grouping.

Vector objects must remain editable after being animated.

### Canvas navigation

Include:

- Zoom.
- Pan.
- Rotate canvas.
- Fit to screen.
- Pixel-level zoom.
- Canvas rotation shortcuts.
- Grid.
- Rulers.
- Guides.
- Snapping.

### Layer types

Support:

- Raster layers.
- Vector layers.
- Traditional animation layers.
- Shape layers.
- Text layers.
- Image layers.
- 3D scene layers.
- Camera layers.
- Null/control layers.
- Adjustment layers.
- Audio layers.
- Precomposition layers.

Layers must support grouping, nesting, visibility, locking, opacity, blending modes, and reordering where appropriate.

---

# 6. Three Integrated Animation Systems

This application MUST support three distinct but interoperable 2D animation workflows.

These systems must not be reduced to one simplified animation model.

## 6.1 Traditional Frame-by-Frame Animation

Implement a proper traditional animation system similar to Toon Boom Harmony.

This is a fundamental feature, not an optional mode.

### Required functionality

- Individual drawings assigned to frames.
- Drawing exposure durations.
- Exposure sheet.
- Drawing substitutions.
- Blank frames.
- Held drawings.
- Frame duplication.
- Frame insertion.
- Frame deletion.
- Drawing replacement.
- Drawing reuse.
- Looping animations.
- Playback controls.
- Onion skinning.
- Adjustable onion skin opacity.
- Adjustable previous and next drawing colors.
- Multiple onion skin frames.
- Previous and next drawing navigation.
- Key pose identification.
- Breakdown identification.
- In-between identification.
- Straight-ahead animation workflow.
- Pose-to-pose animation workflow.
- Drawing-based animation layers.
- Exposure editing.
- Timeline scrubbing.
- Frame-by-frame playback.

### Exposure sheet

Provide a dedicated exposure sheet that clearly displays which drawing is visible at each frame.

Users must be able to:

- Extend drawing exposure.
- Shorten exposure.
- Duplicate exposures.
- Replace drawings.
- Insert blank exposures.
- Create holds.
- Reuse drawings.
- Loop drawings.
- Edit drawing timing.

The exposure sheet must remain connected to the main timeline.

### Drawing management

Each drawing should have its own persistent identity.

A drawing may be exposed across multiple frames without creating unnecessary duplicate image data.

Users must be able to edit the original drawing and have the changes reflected in all exposures that reference it.

### Onion skinning

Implement a proper onion skin system.

Users should be able to configure:

- Number of previous drawings.
- Number of future drawings.
- Opacity.
- Color tint.
- Whether held drawings appear.
- Whether onion skinning is limited to the active layer.

Onion skinning must work with traditional drawing layers and must not interfere with normal rendering.

---

## 6.2 Animate-Style Timeline Animation

Implement a timeline workflow inspired by Adobe Animate.

### Required functionality

- Blank keyframes.
- Standard keyframes.
- Frame spans.
- Motion tweens.
- Classic tweens.
- Shape tweens.
- Symbols.
- Graphic symbols.
- Movie clip symbols.
- Button-style symbols where useful.
- Nested symbols.
- Reusable animation assets.
- Symbol editing mode.
- Symbol instances.
- Independent symbol timelines.
- Tween easing.
- Motion paths.
- Frame-by-frame and tween animation on the same timeline.

### Motion tweens

Allow users to animate objects between keyframes using tween interpolation.

Support:

- Position.
- Scale.
- Rotation.
- Opacity.
- Color.
- Skew where applicable.
- Motion paths.
- Rotation direction.
- Easing curves.

### Shape tweens

Allow editable vector shapes to transition between compatible shapes.

Provide appropriate controls for shape correspondence and interpolation.

Do not fake shape tweening by simply fading one shape into another.

### Symbols and nested timelines

Symbols must be reusable objects with their own timelines.

A symbol should be able to contain:

- Raster artwork.
- Vector artwork.
- Traditional animation.
- Tween animation.
- Other symbols.
- Text.
- Effects.

Nested symbols must be supported without breaking timeline evaluation.

---

## 6.3 After Effects-Style Property Animation

Implement a complete property-based animation engine.

This is separate from traditional frame-by-frame animation.

It should provide professional animation controls similar to After Effects.

### Animatable properties

Support keyframe animation for:

- Position X.
- Position Y.
- Position Z.
- Scale X.
- Scale Y.
- Scale Z.
- Rotation X.
- Rotation Y.
- Rotation Z.
- Anchor point.
- Opacity.
- Visibility.
- Path geometry.
- Stroke width.
- Fill color.
- Stroke color.
- Gradient properties.
- Text properties.
- Font size.
- Text tracking.
- Text rotation.
- Shape properties.
- Effect parameters.
- Blur.
- Glow.
- Shadow.
- Color correction.
- Camera properties.
- Light properties.
- Material properties.
- 3D object transforms.
- Audio volume and other suitable audio parameters.

Properties must be expandable in the timeline.

### Keyframe functionality

Support:

- Automatic keyframing.
- Manual keyframe creation.
- Keyframe deletion.
- Keyframe duplication.
- Keyframe copying and pasting.
- Keyframe movement.
- Keyframe interpolation.
- Linear interpolation.
- Hold interpolation.
- Bezier interpolation.
- Ease in.
- Ease out.
- Ease in/out.
- Custom easing.
- Temporal interpolation.
- Spatial interpolation where appropriate.

### Unified property model

Create a typed property animation system.

Each animatable property should have:

- Persistent property identifier.
- Data type.
- Default value.
- Current evaluated value.
- Keyframe collection.
- Interpolation mode.
- Tangent information.
- Serialization support.

The engine must support suitable data types including:

- Float.
- Integer.
- Boolean.
- Vector2.
- Vector3.
- Vector4.
- Color.
- Rotation.
- Transform.
- Path.
- Enumerated values.

The animation system must be capable of animating both 2D and 3D objects.

Do not build separate incompatible keyframe engines for 2D and 3D.

Use a shared animation evaluation system with property-specific interpolation and validation.

---

# 7. Professional Timeline System

The timeline is one of the most important parts of the application.

It must support all animation workflows in the same project.

### Timeline requirements

- Multiple layers.
- Hierarchical layers.
- Nested groups.
- Expandable property tracks.
- Traditional animation exposures.
- Blank keyframes.
- Standard keyframes.
- Motion tween spans.
- Shape tween spans.
- Property keyframes.
- 3D animation tracks.
- Audio waveforms.
- Markers.
- Work area.
- Playhead.
- Frame numbers.
- Time display.
- Timeline zoom.
- Timeline scrolling.
- Snapping.
- Frame selection.
- Multi-frame selection.
- Copy and paste.
- Undo and redo.

### Timeline organization

A layer may contain multiple expandable properties.

For example:

3D Model
- Transform
  - Position X
  - Position Y
  - Position Z
  - Rotation X
  - Rotation Y
  - Rotation Z
  - Scale X
  - Scale Y
  - Scale Z
- Visibility
- Materials
- Animation Clip
- Effects

A 2D object might contain:

Character
- Transform
  - Position
  - Scale
  - Rotation
  - Opacity
- Fill
- Stroke
- Effects
- Traditional Animation Exposures

The timeline must display the appropriate representation for each animation system.

Do not force traditional animation exposures to become ordinary property keyframes.

Do not force property keyframes to become traditional drawing exposures.

They must coexist and interact through the shared composition and playback system.

### Playback

Include:

- Play.
- Pause.
- Stop.
- Previous frame.
- Next frame.
- First frame.
- Last frame.
- Loop playback.
- Work area playback.
- Playback speed controls.
- Frame-rate selection.
- Real-time playback where hardware permits.

---

# 8. Graph Editor

Implement a professional Graph Editor inspired by After Effects.

The Graph Editor must provide:

- Value graph.
- Speed graph.
- Keyframe selection.
- Bezier handles.
- Tangent editing.
- Ease controls.
- Temporal interpolation.
- Spatial interpolation.
- Graph zoom.
- Graph pan.
- Multiple property graphs.
- Color-coded properties.
- Keyframe alignment.
- Handle manipulation.
- Separate X/Y/Z property graphs.
- Curve editing.
- Copy and paste keyframes.

Users must be able to precisely control animation timing and movement.

### Motion paths

For spatial properties, display motion paths directly in the composition viewport.

Allow users to:

- Select path points.
- Move path points.
- Adjust Bezier handles.
- Add points where supported.
- Edit spatial interpolation.
- Preview the resulting movement.

Graph Editor changes must update the animation engine immediately.

---

# 9. Unified 2D and 3D Composition Architecture

This is one of the most important architectural requirements.

The application must support 2D and 3D elements inside the same composition.

Do not implement 3D as a completely separate application or isolated project type.

A user should be able to create a composition containing:

- Raster drawings.
- Vector artwork.
- Traditional frame-by-frame animation.
- Animate-style symbols and tweens.
- Text.
- Images.
- Video.
- 2D effects.
- Imported 3D models.
- 3D cameras.
- 3D lights.
- 3D materials.
- 3D animation.
- Audio.

All elements must be accessible through the same project, composition, timeline, and playback architecture.

### Example workflow

A user should be able to:

1. Import a 3D building or environment.
2. Place it in a 3D scene.
3. Add a 3D camera.
4. Animate the camera moving through the environment.
5. Add 3D lights.
6. Animate the lights.
7. Create a traditional frame-by-frame animated character.
8. Place that character in the same composition.
9. Animate the character's position and scale using property keyframes.
10. Add vector artwork and animated text.
11. Add effects and compositing.
12. Add sound effects or music.
13. Render everything as one final video.

The original 2D drawings must remain editable.

The imported 3D models must remain editable as 3D scene objects.

The user must not need to render the 3D scene to a video first and import it as a flattened background simply to combine it with 2D animation.

### Shared composition engine

Create a unified composition model that can contain both 2D and 3D elements.

The composition engine must manage:

- Layer ordering.
- 2D and 3D coordinate systems.
- Transform hierarchies.
- Cameras.
- Scene visibility.
- Object visibility.
- Alpha and transparency.
- Rendering order.
- Depth information.
- Compositing.
- Effects.
- Time evaluation.
- Audio synchronization.

### 2D and 3D coordinate systems

Implement appropriate coordinate transformations between:

- 2D composition coordinates.
- 3D world coordinates.
- Camera coordinates.
- Screen coordinates.

Support 3D objects positioned in world space and 2D elements positioned in composition space.

Provide controls for positioning 2D elements relative to 3D scenes where practical.

### 2D and 3D rendering

The rendering architecture must support:

- 2D rendering.
- 3D rendering.
- Transparent 3D rendering.
- Alpha compositing.
- Depth-aware rendering.
- 2D overlays.
- 3D scene backgrounds.
- Layered compositing.
- Render passes.
- Render caching.

The application must be able to composite 2D and 3D elements without unnecessarily flattening editable content.

### Unified playback and timing

All elements must use a shared composition clock.

Traditional animation drawings, tween animation, property keyframes, imported 3D animation clips, camera animation, and audio must remain synchronized.

Support:

- Common frame rate.
- Common time display.
- Frame-accurate evaluation.
- Looping.
- Playback ranges.
- Time remapping where appropriate.
- Animation clip offsets.
- Animation clip duration controls.

Use a robust frame/time representation that avoids floating-point timing drift.

---

# 10. Full 3D Scene Editor

Implement a full 3D scene editor focused on importing, arranging, animating, lighting, and rendering externally created 3D models.

**Modeling requirement: External models only.**

The application does not need to become a full 3D modeling package. Do not prioritize building a mesh modeling, sculpting, or UV authoring suite.

However, the 3D scene editor itself must be fully functional and professionally usable.

### 10.1 3D viewport

Create a dedicated 3D viewport with:

- Perspective view.
- Orthographic views.
- Top view.
- Front view.
- Side view.
- Camera view.
- Orbit controls.
- Pan controls.
- Zoom controls.
- Grid.
- Axis indicators.
- Transform gizmos.
- Object selection.
- Multi-object selection.
- Bounding boxes.
- Wireframe view.
- Solid view.
- Material preview.
- Rendered preview.
- Viewport lighting controls.

Support standard 3D viewport navigation.

Allow users to frame selected objects and reset the view.

### 10.2 External 3D model importing

Support importing externally created 3D assets.

Prioritize:

- GLB.
- glTF.
- FBX.
- OBJ.

Consider later support for:

- STL.
- USD.
- Additional formats through import plugins.

Preserve as much supported asset information as possible:

- Mesh geometry.
- Object hierarchy.
- Materials.
- Textures.
- UV coordinates.
- Normals.
- Tangents.
- Skeletal rigs.
- Skin weights.
- Embedded animation.
- Animation clips.
- Cameras where supported.
- Lights where supported.

Provide useful import options and error reporting.

If a file contains unsupported features, explain what was not imported rather than silently discarding it.

### 10.3 Scene hierarchy

Provide a dedicated 3D scene hierarchy.

Users must be able to:

- Create objects.
- Delete objects.
- Rename objects.
- Group objects.
- Parent objects.
- Unparent objects.
- Duplicate objects.
- Hide objects.
- Lock objects.
- Select multiple objects.
- Organize objects into collections.
- Reorder hierarchy elements where appropriate.

Support imported model hierarchies.

Allow users to select individual objects or parts of imported model hierarchies when supported.

### 10.4 Transform tools

Provide standard 3D transform controls:

- Move.
- Rotate.
- Scale.
- Local transform.
- Global transform.
- Pivot/anchor controls.
- Numeric transform entry.
- Transform reset.
- Transform copying and pasting.

Support X, Y, and Z axes.

All transform properties must be animatable through the shared property animation engine.

### 10.5 Materials and textures

Although users will import externally created models, they must be able to adjust materials inside the application.

Support:

- Base color.
- Metallic.
- Roughness.
- Opacity.
- Emission.
- Normal maps.
- Texture maps.
- Texture assignment.
- Texture replacement.
- Material assignment.
- Material duplication.
- Material presets.
- Material preview.

Support common PBR material workflows.

Allow users to animate suitable material properties.

Do not require users to return to external software for basic material adjustments.

### 10.6 Lighting

Implement a functional 3D lighting system.

Support:

- Directional lights.
- Point lights.
- Spotlights.
- Area lights where supported by the renderer.
- Environment lighting.
- HDRI environments where supported.
- Light intensity.
- Light color.
- Light position.
- Light rotation.
- Shadow controls.
- Light visibility.

Light properties must be animatable.

### 10.7 Cameras

Implement a functional 3D camera system.

Support:

- Perspective cameras.
- Orthographic cameras.
- Multiple cameras.
- Camera selection.
- Camera switching.
- Camera positioning.
- Camera rotation.
- Focal length.
- Field of view.
- Clipping planes.
- Depth of field where supported.
- Camera preview.
- Camera animation.

Camera properties must be accessible in the shared timeline and Graph Editor.

Allow users to animate camera movement and create camera paths.

### 10.8 Imported skeletal animation

Support imported skeletal rigs and animation clips where the file format and renderer permit.

Users should be able to:

- View imported skeletons.
- Select animation clips.
- Preview animations.
- Adjust playback speed.
- Set animation start and end.
- Loop clips.
- Blend between compatible animation clips where practical.
- Animate clip timing.
- Combine imported animation with object-level property animation.

Do not require users to create or edit complex skeletal rigs manually.

Preserve the original imported rig and animation data.

### 10.9 3D animation timeline

Imported 3D objects must integrate with the main application timeline.

Expose animatable properties such as:

- Position X/Y/Z.
- Rotation X/Y/Z.
- Scale X/Y/Z.
- Visibility.
- Material parameters.
- Light intensity.
- Light color.
- Camera properties.
- Animation clip selection.
- Animation speed.
- Animation weight where supported.

Imported animation clips must be usable inside compositions.

Users should be able to position, offset, loop, trim, and time-remap imported animation clips where practical.

### 10.10 3D scene rendering

Support:

- Real-time viewport rendering.
- Shadows.
- Environment lighting.
- Ambient occlusion where supported.
- Reflections where supported.
- Transparency.
- Depth information.
- Anti-aliasing.
- Texture filtering.
- Render quality controls.
- Offline or higher-quality final rendering where practical.

The 3D renderer must work with the shared composition and rendering architecture.

### 10.11 3D scope exclusions

Do not prioritize building:

- Full mesh modeling tools.
- Sculpting.
- Retopology.
- Advanced UV editing.
- Advanced procedural geometry.
- Full physics simulation.
- Full node-based material authoring.

These are outside the initial scope.

The application should focus on being an excellent 3D scene assembly, animation, and compositing environment using externally created models.

---

# 11. Compositing and Effects

Implement a professional 2D and 3D compositing system.

### Layer and compositing features

Support:

- Layer opacity.
- Blend modes.
- Alpha compositing.
- Track mattes.
- Alpha mattes.
- Luma mattes.
- Layer masks.
- Clipping masks.
- Adjustment layers.
- Nested compositions.
- Precompositions.
- Null objects.
- Parent-child relationships.
- 2D and 3D layer interaction.

### Effects

Provide a modular effects system.

Prioritize:

- Gaussian blur.
- Directional blur.
- Glow.
- Drop shadow.
- Color correction.
- Brightness and contrast.
- Hue and saturation.
- Levels.
- Curves.
- Sharpen.
- Noise.
- Vignette.
- Distortion.
- Transform.
- Opacity.
- Masking.

Effects must have editable parameters.

Where practical, effect parameters must be animatable using the shared property animation system.

### Render passes

Design the architecture to support separate render passes where appropriate, including:

- Color.
- Alpha.
- Depth.
- Normal.
- Shadow.
- Object or material identifiers where supported.

These should be available for advanced compositing and future expansion.

---

# 12. 2D Camera System

Provide a proper 2D camera system.

Support:

- Camera position.
- Camera scale.
- Camera rotation.
- Camera bounds.
- Camera movement.
- Camera zoom.
- Camera keyframes.
- Camera easing.
- Camera paths.

The 2D camera must integrate with the property animation engine.

Support compositions that contain 2D camera movement alongside 3D cameras.

---

# 13. Character Rigging and Deformation

Design the architecture to allow future character rigging.

Potential functionality:

- 2D bones.
- IK.
- Controllers.
- Parent-child bone relationships.
- Deformable meshes.
- Puppet-style animation.
- Rig presets.

Do not allow future rigging features to compromise the current traditional frame-by-frame animation system.

Traditional drawing animation must remain fully usable even when rigging is introduced.

---

# 14. Audio System

Implement audio tracks in the main timeline.

Support:

- WAV.
- MP3.
- Additional common audio formats through FFmpeg.

Provide:

- Audio import.
- Waveform display.
- Audio playback.
- Volume control.
- Audio trimming.
- Audio positioning.
- Audio looping.
- Audio synchronization.
- Audio scrubbing where practical.
- Audio property keyframes.
- Multiple audio tracks.

Audio must remain synchronized with the composition frame rate and playback clock.

---

# 15. Project and Document Management

Create a robust native project format.

Suggested extension:

`.studio`

Use a versioned project container, such as a ZIP archive containing:

- Project manifest.
- Scene data.
- 2D drawing assets.
- Vector data.
- Traditional animation drawings.
- Exposure sheet data.
- Timeline data.
- Property keyframes.
- Graph Editor data.
- Imported 3D asset references or embedded assets.
- Materials.
- Textures.
- Animation clip references.
- Audio assets.
- Composition settings.
- Workspace settings.

Use JSON or another suitable structured format for project metadata.

### Project requirements

- New project.
- Open project.
- Save project.
- Save as.
- Autosave.
- Crash recovery.
- Project backup.
- Project versioning.
- Missing asset detection.
- Asset relinking.
- Project validation.

All editable content must remain editable after saving and reopening.

Do not flatten scenes, drawings, animation, or imported 3D models into rendered images as the default project storage method.

Support project migration when the native project format changes between application versions.

### Undo and redo

Implement a robust undo/redo system.

Support undo/redo for:

- Drawing operations.
- Vector edits.
- Layer changes.
- Timeline edits.
- Exposure changes.
- Keyframe edits.
- Graph Editor changes.
- Object transforms.
- 3D scene edits.
- Material changes.
- Camera changes.
- Compositing changes.

Undo/redo must work consistently across the application.

---

# 16. Import and Export

### Image import

Support:

- PNG.
- JPEG.
- BMP.
- TIFF.
- WebP.
- GIF.
- SVG.

PSD support may be considered for a later phase.

### Video import

Use FFmpeg for suitable video formats.

Support common video formats and codecs where licensing permits.

### 3D import

Support:

- GLB.
- glTF.
- FBX.
- OBJ.

Preserve supported materials, textures, hierarchy, and animation data.

### Export

Support:

- PNG.
- JPEG.
- TIFF.
- GIF.
- WebP.
- MP4.
- WebM.
- Image sequences.
- WAV.

Use FFmpeg for video encoding.

Provide suitable codec and quality options.

### Rendering options

Support:

- Full composition rendering.
- Work area rendering.
- Frame-range rendering.
- Image sequence rendering.
- Resolution selection.
- Frame-rate selection.
- Output quality settings.
- Background render queue.
- Render progress.
- Render cancellation.
- Error reporting.

The final renderer must correctly composite 2D and 3D content.

Provide a consistent final render pipeline regardless of whether the composition contains only 2D, only 3D, or both.

---

# 17. Performance and Stability

The application must be designed for professional creative workloads.

### Performance requirements

- GPU-accelerated rendering where available.
- CPU fallback where appropriate.
- Efficient scene graph evaluation.
- Render caching.
- Drawing caching.
- Texture caching.
- Multithreaded processing.
- Background rendering.
- Efficient timeline evaluation.
- Efficient playback.
- Large project handling.
- Memory management.
- Incremental updates.

Avoid unnecessary full-scene redraws.

Use suitable GPU resource management for 2D and 3D content.

Make performance scalable for projects containing many layers, drawings, keyframes, effects, and imported models.

### Hardware considerations

Develop and test on a range of hardware.

The application should work on systems with modest GPUs, while taking advantage of more capable GPUs when available.

Do not make the application dependent on one specific graphics card or vendor.

### Stability

Implement:

- Crash recovery.
- Autosave.
- Project integrity checks.
- Safe asset loading.
- Import validation.
- Rendering error handling.
- Memory safeguards.
- Clear error messages.

---

# 18. Keyboard Shortcuts and Workflow

Provide customizable keyboard shortcuts.

Include common shortcuts for:

- Selection.
- Drawing tools.
- Brush size.
- Undo/redo.
- Save.
- Zoom.
- Pan.
- Timeline navigation.
- Playback.
- Frame insertion.
- Keyframe creation.
- Onion skin toggling.
- Graph Editor navigation.
- 3D viewport navigation.
- Transform tools.
- Object selection.

Provide shortcut presets where practical.

---

# 19. Application Settings

Provide a settings system with:

- General settings.
- Appearance.
- Workspace settings.
- Keyboard shortcuts.
- Tablet settings.
- Brush settings.
- Canvas settings.
- Timeline settings.
- Animation settings.
- Playback settings.
- Rendering settings.
- 3D viewport settings.
- GPU settings.
- Import/export settings.
- Autosave settings.
- Performance settings.

Settings must persist between application launches.

---

# 20. Cross-Platform Development and Packaging

The application must support:

- Windows.
- macOS.
- Linux.

Use CMake and Qt's supported deployment workflows.

### Windows

Produce:

- Portable build.
- Installer.

Use an appropriate installer system such as Inno Setup or WiX.

### macOS

Produce:

- `.app` bundle.
- `.dmg` installer.

Support Apple Silicon and Intel where practical.

Use appropriate signing and notarization workflows when certificates are available.

### Linux

Produce:

- AppImage.
- DEB.
- RPM.

Use CI workflows to build and test supported platforms.

Do not claim that a build works unless it has actually been built and tested.

Document platform-specific dependencies and known limitations.

---

# 21. Development Phases

Build this application incrementally.

Do not attempt to implement the entire application in one massive coding operation.

Each phase must result in a working, testable application.

Do not create placeholder buttons or simulated features to make a phase appear complete.

## Phase 1 — Foundation and Drawing

Implement:

- Project structure.
- Application shell.
- Main interface.
- Workspace system.
- Document model.
- Scene graph foundation.
- Basic raster drawing.
- Basic vector drawing.
- Layers.
- Canvas navigation.
- Save/load.
- Undo/redo.
- Initial cross-platform build configuration.

**Acceptance criteria:**

A user can create a project, draw with raster and vector tools, organize layers, save the project, close the application, reopen it, and continue editing.

## Phase 2 — Traditional Animation

Implement:

- Traditional animation layers.
- Drawing exposures.
- Exposure sheet.
- Drawing substitutions.
- Blank frames.
- Holds.
- Frame insertion and deletion.
- Onion skinning.
- Playback.
- Timeline navigation.

**Acceptance criteria:**

A user can create a real frame-by-frame animation, edit its drawings, change exposure timing, use onion skinning, and play the animation.

## Phase 3 — Property Animation Engine

Implement:

- Typed animatable properties.
- Keyframes.
- Interpolation.
- Automatic keyframing.
- Manual keyframing.
- Transform animation.
- Property tracks.
- Timeline integration.

**Acceptance criteria:**

A user can animate 2D object position, scale, rotation, and opacity with editable keyframes and play the result.

## Phase 4 — Graph Editor

Implement:

- Value graph.
- Speed graph.
- Bezier handles.
- Tangents.
- Easing.
- Spatial motion paths.
- Graph editing.

**Acceptance criteria:**

A user can change animation curves and tangents and immediately see the resulting movement in playback.

## Phase 5 — Animate-Style Tweening

Implement:

- Motion tweens.
- Classic tweens.
- Shape tweens.
- Symbols.
- Nested symbols.
- Reusable animation assets.
- Independent symbol timelines.

**Acceptance criteria:**

A user can create symbols, animate them using tweens, edit their internal timelines, and combine them with traditional animation and property keyframes.

## Phase 6 — 3D Scene Editor

Implement:

- 3D viewport.
- 3D scene hierarchy.
- External model importing.
- GLB/glTF support.
- FBX support.
- OBJ support.
- Object selection.
- Transform gizmos.
- 3D transforms.
- Materials.
- Textures.
- Lighting.
- Cameras.
- Scene management.
- Viewport rendering.

**Acceptance criteria:**

A user can import a 3D model, arrange objects in a scene, change materials, add lights and cameras, save the project, reopen it, and continue editing the scene.

Do not begin building advanced modeling tools. External models are the intended workflow.

## Phase 7 — 3D Animation Integration

Implement:

- 3D property animation.
- 3D timeline tracks.
- Camera animation.
- Light animation.
- Material animation.
- Imported skeletal animation.
- Animation clip management.
- Looping and time offsets.
- Graph Editor support for 3D properties.

**Acceptance criteria:**

A user can animate a 3D object, camera, and light using the same timeline and Graph Editor used for 2D animation.

## Phase 8 — Unified 2D/3D Composition

Implement:

- Shared composition model.
- 2D and 3D scene integration.
- Shared composition clock.
- 2D overlays.
- 3D backgrounds.
- Depth handling.
- Alpha compositing.
- Render passes.
- Unified playback.
- Unified timeline.
- Combined rendering.

**Acceptance criteria:**

A user can place traditional 2D animation, vector artwork, text, and imported 3D models in one composition, animate them together, preview the result, save it, reopen it, and render the entire composition correctly.

This phase is a core milestone, not an optional enhancement.

## Phase 9 — Compositing and Effects

Implement:

- Blend modes.
- Masks.
- Track mattes.
- Adjustment layers.
- Precompositions.
- Effects.
- Render passes.
- 2D and 3D compositing.

**Acceptance criteria:**

A user can build a layered composition with 2D and 3D elements, apply effects, animate effect parameters, and render the final result.

## Phase 10 — Audio and Advanced Workflow

Implement:

- Audio import.
- Waveforms.
- Audio tracks.
- Playback synchronization.
- Audio editing.
- Audio animation properties.
- Additional workflow improvements.
- Performance optimization.

## Phase 11 — Export and Release

Implement:

- Image export.
- Video export.
- Image sequences.
- Render queue.
- FFmpeg integration.
- Windows packaging.
- macOS packaging.
- Linux packaging.
- Automated tests.
- Documentation.
- Release builds.

**Acceptance criteria:**

The application can save and reopen complex projects, render complete 2D/3D compositions, and produce working distributable builds for the supported platforms.

---

# 22. Testing Requirements

Create automated tests throughout development.

Test:

- Project creation.
- Project saving and loading.
- Project migration.
- Drawing data persistence.
- Layer management.
- Traditional animation exposure evaluation.
- Onion skinning.
- Keyframe evaluation.
- Tween evaluation.
- Graph interpolation.
- Nested symbol evaluation.
- 3D asset importing.
- 3D hierarchy persistence.
- Material persistence.
- Camera evaluation.
- Light evaluation.
- Imported animation playback.
- 2D/3D composition evaluation.
- Audio synchronization.
- Rendering.
- Export.
- Undo/redo.

Add integration tests for the most important user workflows.

Do not rely exclusively on unit tests. Run the actual application and verify that the UI, viewport, timeline, and rendering systems work together.

---

# 23. Important Engineering Rules

These rules are mandatory.

1. Build real functionality, not mockups.
2. Do not create buttons that do nothing.
3. Do not simulate keyframes or animation.
4. Do not use fake 3D previews in place of an actual 3D renderer.
5. Do not replace imported 3D models with static images.
6. Do not flatten editable content unnecessarily.
7. Do not remove traditional frame-by-frame animation to simplify the architecture.
8. Do not force traditional animation into the property-keyframe system.
9. Do not create incompatible separate timeline engines for 2D and 3D.
10. Use one shared composition clock.
11. Use one unified property animation architecture for applicable 2D and 3D properties.
12. Preserve the editable source data for every animation system.
13. Keep rendering, animation, document, and UI code modular.
14. Build frequently.
15. Test after every major feature.
16. Fix errors before moving to the next development phase.
17. Document major architectural decisions.
18. Maintain a dependency and third-party license inventory.
19. Never claim that a feature is finished if it is only partially implemented.
20. Never claim that a platform build works unless it has been tested.

If a feature cannot be completed within the current phase, document the limitation and implement the necessary architectural foundation without pretending the feature is functional.

---

# 24. Suggested Project Structure

Use a modular structure similar to:

```text
Application/
    Core/
        Application
        Document
        Project
        Settings
        UndoRedo

    Scene/
        SceneGraph
        Layer
        Object
        Transform
        Composition

    Drawing/
        Canvas
        Raster
        Vector
        Brushes
        Tablet

    Animation/
        AnimationEngine
        PropertySystem
        Keyframes
        Interpolation
        Tweening
        Symbols
        TraditionalAnimation
        ExposureSheet
        OnionSkin
        GraphEditor
        AnimationClips

    Graphics/
        Renderer2D
        Renderer3D
        RenderPipeline
        Compositor
        RenderCache
        Effects
        RenderPasses

    ThreeD/
        Viewport
        Scene
        ModelImporter
        Materials
        Textures
        Lighting
        Cameras
        SkeletalAnimation
        AnimationClips

    Timeline/
        Timeline
        Tracks
        Playback
        Markers
        AudioTracks

    Audio/
        AudioEngine
        Waveform
        Synchronization

    Assets/
        AssetManager
        Importers
        Exporters
        AssetReferences

    UI/
        MainWindow
        Workspaces
        Panels
        Tools
        Inspectors
        Viewports

    Export/
        ImageExport
        VideoExport
        RenderQueue

    Tests/
        Unit
        Integration
```

This is a suggested organization. Adjust it when necessary, but keep the architecture modular and understandable.

---

# 25. Initial Task — Begin Development

Before writing substantial code:

1. Inspect the development environment.
2. Identify the available compilers and build tools.
3. Check installed Qt versions.
4. Check graphics API availability.
5. Evaluate the 2D renderer.
6. Evaluate the 3D renderer.
7. Evaluate model-importing libraries.
8. Review dependency licenses.
9. Confirm Windows, macOS, and Linux build requirements.
10. Propose the final architecture.
11. Create a detailed implementation roadmap.
12. Identify technical risks and dependencies.

Then scaffold the application.

Begin implementing Phase 1 immediately after the architecture and dependencies are established.

Do not spend the entire first phase producing documentation or planning without creating a working application.

Create a working project, compile it, run it, test it, and report exactly what works.

Maintain a development status document that tracks:

- Completed features.
- Partially completed features.
- Unimplemented features.
- Known bugs.
- Build status.
- Test results.
- Current development phase.
- Next tasks.

Update this document as development progresses.

---

# 26. Final Product Vision

The finished application should be a professional creative environment where an artist can:

- Draw and paint illustrations.
- Create vector artwork.
- Animate characters frame by frame.
- Use traditional exposure sheets and onion skinning.
- Create Animate-style motion and shape tweens.
- Build reusable symbols and nested animation.
- Animate individual properties using After Effects-style keyframes.
- Fine-tune movement in a professional Graph Editor.
- Import and arrange external 3D models.
- Create complete 3D scenes with cameras, lights, and materials.
- Animate 3D objects and imported animation clips.
- Combine 2D and 3D elements in one composition.
- Add text, effects, masks, and compositing.
- Synchronize animation with audio.
- Render professional-quality images, sequences, and videos.

**The defining feature of this application is the unified workflow.**

Traditional frame-by-frame animation, Animate-style tweening, After Effects-style property animation, and 3D scene animation must all be first-class systems.

They must work together through one shared project structure, composition engine, timeline, property system, playback engine, and rendering pipeline.

A user should be able to move freely between drawing, animation, motion graphics, compositing, and 3D scene creation without having to export flattened intermediate files or switch to a different project.

Build this as a serious, extensible, professional desktop application—not a demonstration.