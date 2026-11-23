# MyndCraft

A cross-platform desktop application for drawing, illustration, 2D animation, motion graphics,
compositing and 3D scene assembly — built around **one project / composition / timeline /
property / render architecture** so traditional frame-by-frame animation, Animate-style tweens,
After Effects-style keyframes and imported 3D can eventually live in the same composition.

The full product specification is in [`MASTER_DEVELOPMENT_PROMPT.md`](MASTER_DEVELOPMENT_PROMPT.md).
**Current status, what works and what does not: [`STATUS.md`](STATUS.md).**

## Phase 1 (this release): foundation and drawing

Raster painting (brush / pencil / eraser with pressure, flow, hardness, spacing, stabilizer), vector
drawing (pen/Bézier, line, rectangle, ellipse, polygon, direct selection, gradients, grouping),
layers (raster / vector / group, nesting, opacity, blend modes, lock, visibility, drag-and-drop),
canvas navigation (zoom / pan / rotate / fit, grid, rulers, guides, snapping), dockable panels
with workspaces, full undo/redo, `.studio` project files with autosave and crash recovery.

## Build

Requirements: C++20 compiler, CMake ≥ 3.21, Qt ≥ 6.4 (Core, Gui, Widgets, Test), libzip, Ninja (optional).

```bash
# Ubuntu 24.04
sudo apt install build-essential cmake ninja-build qt6-base-dev libzip-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure   # headless tests
./build/myndcraft                                                      # run
./build/myndcraft --screenshot out.png                                 # render the window and exit
```

Only Linux (Ubuntu 24.04, GCC 13, Qt 6.4.2) has been built and tested so far. See `STATUS.md`.

## Shortcuts

| Tools | | Edit / View | |
|---|---|---|---|
| V Selection · A Direct selection | | Ctrl+Z / Ctrl+Shift+Z undo / redo | |
| B Brush · N Pencil · E Eraser | | `[` `]` brush size · X swap colors | |
| P Pen · L Line · M Rectangle · O Ellipse · Y Polygon | | Ctrl+= / Ctrl+- / Ctrl+0 / Ctrl+1 zoom / fit / 100 % | |
| K Bucket · G Gradient · I Eyedropper | | Ctrl+Alt+←/→ rotate canvas · Ctrl+Alt+↑ reset | |
| H Hand (or hold Space / middle mouse) · Z Zoom | | Ctrl+' grid · Ctrl+R rulers · Ctrl+; snap | |
| Ctrl+G / Ctrl+Shift+G group / ungroup shapes | | Alt+click a guide removes it | |

Pen tool: click = corner point, click-drag = smooth point, click the first point to close,
Enter / double-click to finish, Esc to cancel, Backspace removes the last point.
