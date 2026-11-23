# Third-party dependency inventory

License details below are from the projects' documentation as understood when this was written;
**verify each against the exact version you ship before any commercial release.**

| Component | Used for | Status | License | Notes |
|---|---|---|---|---|
| Qt 6 (Core, Gui, Widgets, Test) | UI, images, painting, tablet input | In use (6.4.2 from Ubuntu) | LGPL-3.0 / GPL-3.0 / commercial | Commercial closed-source distribution requires dynamic linking + LGPL obligations, or a commercial Qt license. Avoid GPL-only Qt add-on modules. |
| libzip | `.studio` ZIP container | In use (1.7.3) | BSD-3-Clause | Links zlib (zlib license). |
| Skia | Future 2D/GPU renderer | Evaluated, not integrated | BSD-3-Clause | See `docs/ARCHITECTURE.md` §Renderer. |
| Google Filament | Future real-time 3D renderer | Evaluated, not integrated | Apache-2.0 | Supports PBR, glTF loading (gltfio), Vulkan/GL/Metal. |
| Assimp | Future FBX/OBJ/glTF import | Evaluated, not integrated | BSD-3-Clause | FBX support exists but is not feature-complete. |
| FFmpeg | Future video/audio import/export | Not integrated (a CLI exists in the dev sandbox but is not used) | LGPL-2.1+ or GPL-2+ depending on build | The configuration matters: x264/x265 builds are GPL. Decide codecs (e.g. use platform encoders / OpenH264 / VP9 / AV1) before shipping. |
