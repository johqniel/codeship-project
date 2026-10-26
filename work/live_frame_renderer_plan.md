# Plan: Live Frame-by-Frame Renderer at 60 FPS

## Goal
Build a small C++ executable inside Swaptube that renders frames continuously and displays them live at a fixed rate (for example, 60 FPS), instead of only writing an offline video.

## 1. Reuse Existing Render Loop Entry Points
- Start from `swaptube/src/Scenes/Scene.cpp` flow:
  - `render_one_frame(...)` already updates timing globals (`t`, `frame_number`, fractions), runs `query()`, and gets the rendered GPU pixel buffer.
- Reuse the same scene objects/pipeline used by existing project scripts (like `OuterBilliardsScene`) to avoid duplicating rendering logic.

## 2. Add a New Live Runner Binary
- Add a new executable target (for example `swaptube_live`) in `swaptube/CMakeLists.txt`.
- Create a new source file (for example `swaptube/src/Core/LiveMain.cpp`) that:
  - Creates a chosen scene (`OuterBilliardsScene` or `CompositeScene`).
  - Initializes scene state similarly to a normal project.
  - Runs an infinite loop at target FPS.

## 3. Add a Display Backend (Window + Texture)
- Use SDL2 (simple and portable) for windowing/display.
- Per frame:
  - Call scene `query()` (or a small wrapper around `render_one_frame` semantics).
  - Copy/interop GPU pixels to a display buffer.
  - Upload to SDL texture and present.
- Keep rendering code isolated from display code so offline pipeline remains unchanged.

## 4. Fixed-FPS Timing Control (60 FPS)
- Use a fixed timestep target:
  - `target_dt = 1.0 / 60.0` seconds.
- Each loop:
  - Record frame start time.
  - Render/present one frame.
  - Sleep for remaining time budget (`target_dt - frame_time`) if positive.
- If a frame overruns:
  - Skip sleep and continue (optionally track dropped/late frames).

## 5. Input + Runtime Controls
- Add minimal controls:
  - `Esc`: quit.
  - Space: pause/resume time progression.
  - `[` / `]`: decrease/increase FPS cap.
- Optional: toggle overlays for current FPS, frame number, and time `t`.

## 6. Keep State/Transition Semantics Compatible
- Preserve existing manager semantics:
  - Continue evaluating transitions every frame through the same state update path.
- For live mode, create a lightweight equivalent of micro/macro progression:
  - Either keep macroblocks disabled and drive pure time-based state.
  - Or implement a simple scheduler that advances transitions over wall-clock time.

## 7. Validation Checklist
- Visual correctness matches offline render for same initial state.
- Stable frame pacing near 60 FPS on target machine.
- No memory growth over long run.
- Clean shutdown of GPU/resources/window.

## 8. Implementation Order (Short)
1. Create `LiveMain.cpp` with scene init + 60 FPS loop.
2. Add SDL2 window/texture presentation.
3. Wire new CMake target and dependencies.
4. Add keyboard controls + FPS stats.
5. Compare one deterministic scene against offline output.

## Notes
- This plan intentionally avoids MP4/audio pipeline dependencies, focusing on interactive visual rendering.
- Once stable, you can add optional recording by piping presented frames into the existing video writer path.
