# TODO — Three-Phase Build Plan for the Next Eye-System Revision

This task list is designed for an agentic coding agent. It is intentionally organized around execution dependencies and implementation flow rather than a strict monotonic phase list.

## Phase 1: Foundation and mode architecture

### A1. Define the runtime mode model
- Update the mode registry so that `normal`, `dozing`, `suspicious`, and `angry` are explicit first-class states.
- Add configuration and state transitions for each mode in the animation layer.
- Keep the current dozing implementation as the baseline while building the larger behavior model.
- Files likely involved: `src/main.cpp`, `src/eye.h`, `src/eye.cpp`, `src/DrowsinessModel.h`

### A2. Split renderer input from renderer logic
- Convert the render pipeline so it consumes a defined input set instead of implicit state.
- Add separate inputs for gaze, lid state, pupil target, head tilt, and expression blend.
- Keep renderer code deterministic and test-friendly.
- Files likely involved: `src/eye.cpp`, `src/eye.h`

### A3. Build the expression profile abstraction
- Introduce an expression/profile object that carries:
  - iris behavior
  - pupil behavior
  - gaze pattern
  - blink rhythm
  - eyelid posture
  - agitation / alertness blend
- Make the system extensible for future realism settings without hardcoding branch logic in `main.cpp`.
- Files likely involved: `src/eye.h`, `src/eye.cpp`, `src/main.cpp`

### A4. Stabilize the dozing baseline
- Confirm the existing dozing behavior remains valid after the refactor.
- Tune the default baseline and recovery to avoid the near-invisible dozing state seen when the system is freshly booted.
- Files likely involved: `src/DrowsinessModel.cpp`, `src/DrowsinessModel.h`, `src/eye.cpp`

### A5. Protect large generated asset files from manual edits
- Treat generated eye headers as build artifacts, not hand-edited source.
- Document the generation path and keep all source art separate from generated output.
- Add a warning in the build docs: large generated tables are expected and should be regenerated rather than edited directly.
- Files likely involved: `create/EYE_ASSET_SPEC.md`, `src/generated/`, `Uncanny_Eyes/convert/`

---

## Phase 2: Realistic eye system and pupil mapping

### B1. Replace the old eyeball assumptions with the normalized 200x200 eye pipeline
- Define the new target eye model: normalized eyeball plate, deterministic pupil detection, and a screen-space render path.
- Keep the old system working only as a compatibility layer until the new model is validated.
- Files likely involved: `create/EYE_ASSET_SPEC.md`, `src/eye.cpp`, any future asset-generation scripts

### B2. Implement the crosshair-based non-round pupil detection
- Use the black crosshair marker rather than a dot so narrow slitted pupils remain valid when the pupil is constricted.
- Compute pupil center and radius deterministically from the normalized marker.
- Ensure the result works for mammalian, reptilian, cat, and goat-like pupil shapes.
- Files likely involved: `src/eye.cpp`, future generation scripts, asset definitions

### B3. Add exact pupil mapping and dilation model
- Build a precise pupil response model that tightens the relationship between light, dilation, and rendering.
- Incorporate constraints for constriction speed, recovery speed, hysteresis, and repeatable adaptation.
- Ensure that the response is not a simple raw ADC pass-through.
- Files likely involved: `src/LDRSerialInput.h`, `src/LDRSerialInput.cpp`, `src/PupilResponseModel.*`, `src/eye.cpp`

### B4. Add realism settings: iris type, pupil type, expression type
- Create a profile-driven system for:
  - iris style
  - pupil geometry
  - expression mood
  - realism level
- Make the profile system abstract enough to avoid requiring new renderer branches for each new eye type.
- Files likely involved: `src/eye.h`, `src/eye.cpp`, `src/main.cpp`

### B5. Validate slitted-pupil behavior under edge conditions
- Test narrowest dilation states and ensure the pupil remains bounded by the actual pupil geometry.
- Check for edge failures that cause the pupil to render outside the visible dark region.
- Add a targeted regression suite or smoke-test checks for these edge states.
- Files likely involved: `src/eye.cpp`, `tests/`, `README.md`

---

## Phase 3: Display, streaming, and operator polish

### C1. Lock the output to the borderless 16:9 stream-friendly mode
- Keep the output shape fixed to 16:9 and borderless for a low-light remote-display environment.
- Remove any needless decoration or desktop bleed from the window composition.
- Files likely involved: `src/main.cpp`, `README.md`, `rtsp_server.py`

### C2. Verify the RTSP/VLC pipeline end-to-end
- Re-test the remote stream path with the new eye output and ensure it stays stable when the app restarts.
- Confirm the capture window logic continues to target only the eye window and not the broader desktop.
- Files likely involved: `rtsp_server.py`, `run_rtsp.sh`, `README.md`

### C3. Add runtime tuning for light response and expressions
- Expose tuning parameters for LDR sensitivity, pupil response, and expression intensity.
- Ensure the TUI can pass these values without rebuilding the binary.
- Files likely involved: `src/main.cpp`, `uncanny-tui.py`, `README.md`

### C4. Expand the operator TUI for mode switching and calibration
- Make the TUI a real control surface for:
  - mode selection
  - design / profile selection
  - light calibration status
  - stream status
  - runtime warnings
- Files likely involved: `uncanny-tui.py`

### C5. Production hardening and visual validation
- Validate the system under live conditions with a real LDR input and remote display.
- Check transition smoothness between normal, dozing, suspicious, and angry states.
- Add a final visual regression pass and document any remaining caveats.
- Files likely involved: `README.md`, `STATE.md`, `BUILD_ROADMAP.md`, `BUILDPLAN.md`

---

## Cross-cutting tasks

### X1. Audit generated eye data sizes and generator churn
- The project currently has very large generated headers (~16k lines each).
- Build a generation strategy that minimizes churn and keeps the source-of-truth assets small and editable.
- Files likely involved: `src/generated/`, `Uncanny_Eyes/convert/`, `create/`

### X2. Add a validation harness for new eye assets
- Build a smoke-test plus visual regression harness for each eye configuration.
- Verify the renderer still produces RGB channels, correct pupil bounds, and no stale data.
- Files likely involved: `src/main.cpp`, `tests/`, `create/`

### X3. Keep documentation aligned with implementation reality
- Ensure the docs reflect the actual architecture, especially the new mode system and the normalized eye pipeline.
- Update examples and launch commands so they match the runtime behavior.
- Files likely involved: `README.md`, `ARCHITECTURE.md`, `STATE.md`, `BUILD_ROADMAP.md`, `BUILDPLAN.md`

---

## Execution order for an agentic workflow

The agent should execute in this dependency order, though not every task is strictly sequential:

1. Define the mode/profile model.
2. Refactor the renderer input contract.
3. Stabilize dozing and normal behavior.
4. Implement the normalized eye asset pipeline and pupil mapping.
5. Tighten LDR-to-pupil response calibration.
6. Add suspicious/angry modes and expression profiles.
7. Validate the RTSP stream and 16:9 display.
8. Improve TUI controls and runtime calibration.
9. Run final visual regression and documentation pass.

This order reduces rework: architecture first, realistic eye behavior second, operator polish third.
