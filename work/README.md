# Workbench: Billiards Visual Render + Fractal Plotting

This folder provides a practical workflow on top of the existing Codeship/SwapTube infrastructure:

1. Render the visual track of the Billiards project.
2. Generate the billiards-inspired fractal maps from Python.

## Files in this Folder

- `render_billiards_visual.sh`: wrapper around `swaptube/go.sh` to run `src/Projects/Billiards.cpp` with **audio disabled**.
- `README.md`: this guide.

## 1) Render the Visual Part of Billiards

The project scene logic lives in `swaptube/src/Projects/Billiards.cpp`. SwapTube needs to run its mandatory smoketest first, then the full render. The helper script does this for you and ensures audio is disabled with `-a`.

From the repository root:

```bash
chmod +x work/render_billiards_visual.sh
./work/render_billiards_visual.sh 1280 720 30 render
```

Arguments:

```text
./work/render_billiards_visual.sh [width] [height] [fps] [render|smoketest]
```

Examples:

```bash
# Full visual render (no narration/sfx)
./work/render_billiards_visual.sh 1920 1080 30 render

# Fast smoke validation only (no encoded video)
./work/render_billiards_visual.sh 640 360 30 smoketest
```

Output:

- Render artifacts are written under `swaptube/out/Billiards/<timestamp>/`.
- The script prints the latest output folder when done.

## 2) Plot Nice Fractals with Existing Infrastructure

You already have `plot_fractals.py` at the project root. It computes a period map for an outer-billiards style dynamical system and writes an animated GIF.

If you only care about fractals, you do **not** need SwapTube, CUDA, or MicroTeX.

Run it from the repository root:

```bash
python3 plot_fractals.py
```

Or on NixOS without setting up Python packages globally:

```bash
chmod +x work/run_fractals_nix.sh
./work/run_fractals_nix.sh
```

By default, it produces:

- `hyperbolic_billiards.gif`
- A progression of curvature/radius values across frames.

### Parameter Knobs That Matter Most

Open `plot_fractals.py` and tune:

- `FRAMES`: animation length and smoothness.
- `GIF_RES` and `RES`: speed vs. detail.
- `N`, `RADIUS`, `PHASE`: polygon shape and orientation.
- `CURVATURE`: geometry regime.
- `MAX_PERIOD`: orbit return-depth coloring richness.

A practical workflow:

1. Start with lower `GIF_RES` and `MAX_PERIOD` for quick experiments.
2. Lock a composition you like (`N`, `PHASE`, `CURVATURE`).
3. Raise `GIF_RES` and `MAX_PERIOD` for final output.

## Recommended Environment Setup

- For SwapTube rendering: follow `swaptube/README.md` dependencies (CMake, Ninja, ffmpeg libs, CUDA/HIP, etc.).
- For fractal plotting:

```bash
python3 -m pip install numpy matplotlib pillow
```

## Troubleshooting

- `go.sh: Required command ... not found`
  - Install missing tools listed in `swaptube/README.md`.
- MicroTeX prompt appears during build
  - Follow the automatic install path or install manually as documented in `swaptube/README.md`.
- Fractal script is slow
  - Lower `GIF_RES`, `RES`, and `MAX_PERIOD` first.

With this setup, you can use SwapTube for high-fidelity scene rendering and Python for fast exploratory fractal generation in the same project.
