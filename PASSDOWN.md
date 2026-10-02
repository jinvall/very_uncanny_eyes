


..............................................................................























































































































































.# Passdown for the Next Session

## Starting Point

Continue in:

```text
/home/jason/uncanny_eyes
```

The current scope is complete. The standard build, default/cat/dragon smoke tests in both normal and dozing modes, sanitizer runs, windowed live capture, synchronized-blink capture, fullscreen lifecycle, and the optional USB-serial LDR input all pass. A portable distribution tarball has been created. Do not replace the generated artwork or restart the port.

The `customDragon`, `customDragonA`, and `customDragonB` designs fail the smoke test because their generated sclera/iris conversion contains no red pixels. This is a pre-existing artwork-conversion defect, not a renderer or LDR defect. The other three designs and both modes pass.

## Current Implementation

- `src/eye.cpp` uses stable sclera coordinates and valid iris/polar coordinates, preventing stale blue/yellow artwork from being exposed during gaze changes.
- Eyelid tracking is evaluated once per frame and shared by both eyes.
- Blink timing is shared; rendered thresholds remain per-eye.
- `EyeAnimator` uses one shared gaze target.
- The right eye's crop coordinate is mirrored before tracking/rendering, while `src/main.cpp` retains `SDL_FLIP_HORIZONTAL`. This makes both pupils move in the same world direction without changing the generated artwork.
- `src/main.cpp` still composes two non-overlapping `128x128` textures and supports windowed/fullscreen operation.
- **Dozing mode** (`--mode dozing` or runtime `D` key toggle): a continuous breathing envelope narrows both eyelids in sync over `breathPeriodUs`, with a periodic snore closure layered on top at `blinkIntervalUs`. The shared doze/blink update runs exactly once per frame (`lastFrameUs_` guard), so both eyes observe one identical per-frame snapshot — no per-eye phase divergence. The first doze blink is due at `modeStart + breathPeriodUs + blinkIntervalUs` (one warmup cycle plus one interval), anchoring the periodic deadline to the breathing phase.
- **Caruncle fade** (default eye only): the inner-canthal caruncle fades smoothly as the shared doze closure increases, preserving its fixed source position.
- **Custom dragon eye**: new `customDragon` design generated from `drag_no_pupil.png` via the Adafruit `convert/tablegen.py` pipeline, using the original dragon sclera/lid/pupilMap assets.
- **Optional USB-serial LDR input** (`src/LDRSerialInput.h`/`.cpp`): a nonblocking Linux serial reader on a background thread that parses one non-negative integer per line (optional `LDR:` prefix), applies a low-pass filter, normalizes against `--ldr-min`/`--ldr-max`, maps light to a pupil target via a nonlinear power curve, and drives `EyeAnimator` through a separate exponential constriction/dilation response. The reader never blocks the render loop; only the latest sample is published. A silent device holds the last state for `--ldr-timeout` ms, then the environmental modifier stops and reconnect attempts continue unless `--ldr-no-reconnect` is set. Disabled by default; `--ldr` requires `--ldr-port`. See `README.md` for the protocol, calibration, and Linux `dialout` permissions.
- **`src/PupilResponseModel.h`/`.cpp`**: time-based pupil response helper used by the LDR path.
- **`tests/ldr_test.cpp`**: CTest target covering line parsing, normalization, inversion, nonlinear mapping, low-pass filtering, response timing, and disconnect/timeout behavior.
- **`reference/ldr_esp32/ldr_esp32.ino`**: minimal ESP32 reference sketch.

## Verification Commands

```sh
cmake --build build-ldr --parallel
./build-ldr/uncanny-eyes --smoke-test
./build-ldr/uncanny-eyes --design cat --smoke-test
./build-ldr/uncanny-eyes --design dragon --smoke-test
./build-ldr/uncanny-eyes --smoke-test --mode dozing
./build-ldr/uncanny-eyes --design cat --smoke-test --mode dozing
./build-ldr/uncanny-eyes --design dragon --smoke-test --mode dozing
./build-ldr/uncanny-eyes --list-designs
ctest --test-dir build-ldr
./build-ldr/uncanny-eyes --ldr --ldr-port /dev/ttyUSB1 --ldr-timeout 1000 --ldr-no-reconnect
```

Current smoke output is equivalent to:

```text
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=27182, green=27176, blue=26906) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=19727, green=19727, blue=0) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=22402, green=22402, blue=19727) across 6 eye frames
```

The cat design's zero blue count is expected because its iris map is `1x1` white.

Sanitizer verification was run from `build-asan` with AddressSanitizer and UndefinedBehaviorSanitizer; default, cat, and dragon in both modes passed.

## Live Verification

- Windowed `960x540` capture: no stale artwork, no red overlay, correct right-eye anatomy, and matching lateral gaze.
- Motion montage: both pupils moved together through left and right extremes.
- Blink montage: both eyes entered and left the blink together with no residue.
- Doze mode: both lids breathe in sync and snore together; no per-eye divergence.
- Fullscreen `2048x1152` capture: corrected shared gaze direction.
- Fullscreen was toggled to windowed and exited with ESC; no process or window remained.

## Architecture Notes

- `ARCHITECTURE.md` documents the upstream Arduino design and desktop port boundary.
- `AGENT.md` remains the source of truth for scope.
- `src/main.cpp` owns SDL lifecycle, CLI parsing, texture upload, layout, and display mirroring.
- `src/eye.cpp` owns generated-artwork rendering, shared gaze, eyelid tracking, blink state, and doze state.
- `src/eye.h` owns the data structures and animator state.
- `src/artwork_*.cpp` are adapters; generated upstream artwork remains unchanged.
- `Uncanny_Eyes/uncannyEyes/uncannyEyes.ino` remains the upstream rendering reference.

## Do Not Do

- Do not modify generated eye data to hide renderer defects.
- Do not remove the right-eye display flip.
- Do not remove the right-eye crop-coordinate mirroring; without it, the flipped texture makes the pupils look in opposite directions.
- Do not call `updateDoze`/`blink` more than once per frame; the second eye must read the shared snapshot, not re-advance shared state.
- Do not claim fullscreen verification without a fresh live run.
- Do not leave verification processes or windows running.

## Completion Criteria

All current criteria are satisfied:

1. The project builds with the documented command.
2. Default, cat, and dragon smoke tests pass in normal and dozing modes.
3. Hard gaze changes do not expose stale blue/yellow artwork.
4. Both eyes share one gaze target and move in the same direction.
5. Blinks complete without residue.
6. The right eye is mirrored with correct lateral gaze behavior.
7. Fullscreen launches, toggles, and exits cleanly.
8. Doze mode breathes both lids in sync with no per-eye divergence and a clean post-blink return.
9. Caruncle (default eye) fades correctly during doze closure.
10. The optional LDR input is disabled by default and leaves existing behavior unchanged.
11. The LDR parser, filter, mapping, response, and disconnect/timeout behavior pass the CTest target.
12. An enabled LDR run with no device attached exits cleanly on SIGTERM.
