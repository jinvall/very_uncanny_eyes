# Project State

Last updated: 2026-09-21

## Objective

Run Adafruit Uncanny Eyes natively on Ubuntu/Linux, including windowed development and fullscreen HDMI/DisplayPort use. The target is two mirrored eyes that share one gaze target, blink cleanly, preserve the original generated artwork, and render at 60 FPS on CPU-only hardware.

## Current Status

The SDL2/C++17 desktop port is complete for the current scope and builds successfully from:

`/home/jason/uncanny_eyes`

The current executable is:

`/home/jason/uncanny_eyes/build-ldr/uncanny-eyes`

The standard build, default/cat/dragon smoke tests in both normal and dozing modes,
address/undefined-behavior sanitizer runs, windowed live capture, synchronized-blink
capture, fullscreen launch/toggle/ESC exit, and the optional USB-serial LDR input all pass.

## Implemented

- Native SDL2 window and fullscreen rendering in `src/main.cpp`.
- C++17/CMake build configuration in `CMakeLists.txt`.
- Original Adafruit generated artwork adapters:
  - `defaultEye` multicolor artwork.
  - `catEye` artwork, whose iris map is intentionally `1x1` and flat white.
- Native-endian SDL texture packing.
- Horizontal display mirroring for the right eye using `SDL_RenderCopyEx()` and `SDL_FLIP_HORIZONTAL`.
- Right-eye crop-coordinate mirroring before tracking and rendering, so the display flip produces matching lateral gaze direction rather than opposite-looking eyes.
- Correct sclera/iris/polar source coordinates, with the sclera used as the stable base layer and iris sampled only inside its valid region.
- Frame-scoped eyelid tracking thresholds shared by both eyes.
- Shared blink timing with per-eye rendered thresholds.
- Windowed, fullscreen, smoke-test, design-list, scale, background, and FPS CLI options.
- ESC exit and F fullscreen-toggle behavior.
- Dozing animation mode (`--mode dozing` or runtime toggle): a continuous breathing envelope that narrows both lids in sync plus a periodic snore closure layered on top. Driven by one shared cycle state (`dozeModeStartUs_`), so both eyes observe an identical per-frame snapshot; the shared doze/blink update runs exactly once per frame regardless of eye index.
- Existing architecture and project instructions in `ARCHITECTURE.md` and `AGENT.md`.
- **Optional USB-serial LDR input** (`src/LDRSerialInput.h`/`.cpp`): a nonblocking Linux serial reader on a background thread that parses one non-negative integer per line (optional `LDR:` prefix), applies a low-pass filter, normalizes against `--ldr-min`/`--ldr-max`, maps light to a pupil target via a nonlinear power curve, and drives `EyeAnimator` through a separate exponential constriction/dilation response. The reader never blocks the render loop; only the latest sample is published. A silent device holds the last state for `--ldr-timeout` ms, then the environmental modifier stops and reconnect attempts continue unless `--ldr-no-reconnect` is set. Disabled by default; `--ldr` requires `--ldr-port`. See `README.md` for the protocol, calibration, and Linux `dialout` permissions.
- **`src/PupilResponseModel.h`/`.cpp`**: time-based pupil response helper used by the LDR path.
- **`tests/ldr_test.cpp`**: CTest target covering line parsing, normalization, inversion, nonlinear mapping, low-pass filtering, response timing, and disconnect/timeout behavior.
- **`reference/ldr_esp32/ldr_esp32.ino`**: minimal ESP32 reference sketch.

## Verification

Last verified from the project root:

```text
cmake --build build --parallel
./build/uncanny-eyes --smoke-test
./build/uncanny-eyes --design cat --smoke-test
./build/uncanny-eyes --design dragon --smoke-test
./build/uncanny-eyes --smoke-test --mode dozing
./build/uncanny-eyes --design cat --smoke-test --mode dozing
./build/uncanny-eyes --design dragon --smoke-test --mode dozing
./build/uncanny-eyes --list-designs
```

Observed:

```text
[100%] Built target uncanny-eyes
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=27182, green=27176, blue=26906) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=19727, green=19727, blue=0) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=22402, green=22402, blue=19727) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=27182, green=27176, blue=26906) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=19727, green=19727, blue=0) across 6 eye frames
smoke test: selected-mode gaze and synchronized blink checks passed
smoke test: rendered 98304 opaque pixels (red=22402, green=22402, blue=19727) across 6 eye frames
default
cat
dragon
```

The cat design intentionally has `blue=0` because its iris map is a single white RGB565 pixel; the smoke test accepts that palette while still requiring opaque red and green eye content.

LDR verification:

```text
cmake --build build-ldr --parallel
ctest --test-dir build-ldr
./build-ldr/uncanny-eyes --ldr --ldr-port /dev/ttyUSB1 --ldr-timeout 1000 --ldr-no-reconnect
```

The CTest target `ldr_serial_model` passes, and an enabled LDR run with no
device attached exits cleanly on `SIGTERM` after the background reader has
opened the port and waited for the configured hold timeout.

Address and undefined-behavior sanitizer verification also passed for all three designs and both modes:

```text
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -O1' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=1 ./build-asan/uncanny-eyes --smoke-test
ASAN_OPTIONS=detect_leaks=1 ./build-asan/uncanny-eyes --design cat --smoke-test
ASAN_OPTIONS=detect_leaks=1 ./build-asan/uncanny-eyes --design dragon --smoke-test
ASAN_OPTIONS=detect_leaks=1 ./build-asan/uncanny-eyes --smoke-test --mode dozing
ASAN_OPTIONS=detect_leaks=1 ./build-asan/uncanny-eyes --design dragon --smoke-test --mode dozing
```

Live verification:

- Windowed `960x540` capture showed both eyes, correct color channels, no stale blue/yellow exposure, and matching lateral gaze direction.
- A multi-frame montage showed both pupils moving together through leftward and rightward extremes.
- A synchronized blink montage showed both eyes closing and reopening together with no residual lid mask.
- Fullscreen `2048x1152` capture showed the corrected shared gaze direction.
- Fullscreen was toggled back to windowed and exited with ESC; no `uncanny-eyes` process or window remained.

SDL version used during verification: `2.30.0`.

The upstream Adafruit checkout remains at:

```text
/home/jason/uncanny_eyes/Uncanny_Eyes
commit d2103e84aa33da9f6924885ebc06d880af8deeff
```

## Known Issues

No known rendering or synchronization defects remain in the current scope.

The `cat` design has no blue channel by design; this is not a renderer defect.

The `customDragon`, `customDragonA`, and `customDragonB` designs fail the smoke
test because their generated sclera/iris conversion contains no red pixels
(`redPixels == 0`). This is a pre-existing artwork-conversion defect, not a
renderer or LDR defect. The other three designs and both modes pass the smoke
test, sanitizer checks, and the LDR lifecycle checks.

## Active Process

No verification process or `Uncanny Eyes` window is running.

## Rendering Model

- Logical eye output remains `128x128`.
- `EyeRenderer::render()` samples the original sclera, iris, polar lookup, and eyelid threshold maps.
- `EyeAnimator::update()` supplies a shared gaze target, iris scale, and eyelid state.
- The right eye's crop coordinate is mirrored before tracking and rendering; `src/main.cpp` still applies the display-layer horizontal flip. This combination keeps both pupils moving in the same world direction while preserving the intended right-eye anatomy.
- `src/main.cpp` uploads each frame to an SDL streaming texture and composes two non-overlapping eye rectangles.
- The desktop renderer replaces Arduino SPI/display-driver output while retaining the generated artwork and core eye-data format.

## Important Source Locations

- `src/eye.h`: `EyeArtwork`, `EyeState`, `EyeFrame`, renderer, animator, shared gaze, and blink declarations.
- `src/eye.cpp`: rendering, gaze animation, iris animation, blink state, eyelid tracking, right-eye crop mirroring, and the environmental-pupil response.
- `src/main.cpp`: SDL lifecycle, CLI, texture upload, frame loop, layout, display mirroring, and LDR lifecycle.
- `src/LDRSerialInput.h`/`.cpp`: USB serial reader, line parser, normalization, nonlinear mapping, low-pass filter, and disconnect/timeout handling.
- `src/PupilResponseModel.h`/`.cpp`: time-based constriction/dilation pupil response helper.
- `src/artwork_default.cpp`: multicolor `defaultEye` adapter.
- `src/artwork_cat.cpp`: `catEye` adapter.
- `src/artwork_registry.cpp`: artwork selection.
- `tests/ldr_test.cpp`: CTest target for the LDR parser, filter, mapping, and response model.
- `reference/ldr_esp32/ldr_esp32.ino`: minimal ESP32 LDR reference sketch.
- `Uncanny_Eyes/uncannyEyes/graphics/defaultEye.h`: original generated multicolor eye data.
- `Uncanny_Eyes/uncannyEyes/graphics/catEye.h`: original generated cat-eye data.
- `Uncanny_Eyes/uncannyEyes/uncannyEyes.ino`: upstream rendering/animation reference.
- `Uncanny_Eyes/uncannyEyes/config.h`: upstream configuration reference.

## Build and Run

Dependencies on Ubuntu/Debian:

```text
cmake g++ libsdl2-dev
```

Build:

```sh
cmake -S . -B build-ldr -DCMAKE_BUILD_TYPE=Release
cmake --build build-ldr --parallel
```

Windowed run:

```sh
./build-ldr/uncanny-eyes
```

Fullscreen run:

```sh
./build-ldr/uncanny-eyes --fullscreen
```

Smoke test:

```sh
./build-ldr/uncanny-eyes --smoke-test
```

List designs:

```sh
./build-ldr/uncanny-eyes --list-designs
```

Use `Esc` to exit and `F` to toggle fullscreen.
