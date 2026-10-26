# live-engine

Interactive C++ live renderer for experimenting with outer billiards in real time.

## Features
- Fixed-rate render loop targeting 60 FPS.
- Live visual feedback while dragging polygon corners.
- Resolution presets including a very-high 4K mode.
- Idle drift mode: after no edits, vertices move with smooth directed drift.
- Initial idle drift is circular (tangential around the polygon center), then direction changes very gradually.
- Hyperbolic toggle with live curvature adjustment.
- CUDA-based frame computation for the flow field.
- Self-contained source dependencies under `src/`.
- Keyboard controls for depth and zoom.

## Build
```bash
cd live-engine
cmake -S . -B build
cmake --build build -j
```

Build requires:
- CUDA toolkit (nvcc)
- X11 development headers

## Run
```bash
./build/live_engine
```

## Controls
- Left mouse drag: move polygon corners.
- `[ / ]`: decrease/increase flow depth.
- `- / =`: zoom out/in.
- `r`: cycle resolution mode (`fast` 1280x720, `high` 1920x1080, `very-high` 3840x2160).
- `y`: toggle hyperbolic mode on/off.
- `b`: play the Billiards hyperbolic sequence (curvature/zoom/shape timeline from Billiards.cpp).
- `, / .`: decrease/increase hyperbolic curvature magnitude.
- `g`: toggle debug vertex handles overlay.
- `j`: toggle idle jitter mode.
- `1`: square preset.
- `2`: pentagon preset.
- `3`: triangle preset.
- `h`: print controls to terminal.
- `Esc` or `q`: quit.

## Live Config File
You can edit `live_config.txt` while the program is running.
The app polls it every 250ms.

Available keys (`key=value`):
- `flow_depth` (int)
- `resolution_mode` (`0` fast, `1` high, `2` very-high)
- `view_half_width` (double)
- `center_x` (double)
- `center_y` (double)
- `hyperbolic_enabled` (`0/1`, `true/false`)
- `curvature` (negative double, used when hyperbolic is enabled)
- `table_opacity` (double in `[0,1]`)
- `island_opacity` (double in `[0,1]`)
- `singularity_opacity` (double in `[0,1]`)
- `singularity_depth` (double)
- `singularity_rainbow` (double in `[0,1]`)
- `periodicity_or_flow` (double in `[0,1]`)
- `cycle_highlight` (double)
- `cycle_highlight_enable` (double in `[0,1]`)
- `show_debug_handles` (`0/1`, `true/false`)
- `idle_jitter_enabled` (`0/1`, `true/false`)
- `idle_jitter_start_delay_sec` (double)
- `idle_drift_speed` (double, tiny values recommended)
- `idle_turn_jitter` (double, tiny values recommended)

Example:
```txt
flow_depth=60
resolution_mode=2
hyperbolic_enabled=1
curvature=-0.05
idle_jitter_enabled=1
idle_jitter_start_delay_sec=3.0
idle_drift_speed=0.00045
idle_turn_jitter=0.00014
```

## Notes
- Hyperbolic table/island/singularity rendering now reuses Swaptube CUDA code vendored in [src/swaptube](src/swaptube).
- The live wrapper that feeds those kernels is in [src/engine](src/engine).
- The app enforces convex CCW polygon ordering while dragging to keep the outer-billiards map stable.
- In very-high mode, CPU load is much higher; use `fast` mode while editing and switch back when framing.
