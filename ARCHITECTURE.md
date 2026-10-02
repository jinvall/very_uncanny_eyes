# Upstream Architecture

This document records the architecture of the Adafruit Uncanny Eyes source tree before the desktop port changes it. The inspected upstream revision is `d2103e84aa33da9f6924885ebc06d880af8deeff` (`origin/master` at inspection time).

## Source layout

- `uncannyEyes/uncannyEyes.ino` contains initialization, the pixel renderer, animation state machine, autonomous iris behavior, and the Arduino `setup()`/`loop()` entry points.
- `uncannyEyes/config.h` selects the eye data, display driver, board-specific pins, eye count, tracking, blinking, iris input, and symmetry options.
- `uncannyEyes/graphics/*.h` are generated C arrays containing the original eye artwork and lookup tables.
- `convert/tablegen.py` converts the PNG source assets into the generated arrays. `convert/hextable.py` formats the numeric output.
- `convert/*/` contains the original raster source assets: sclera, iris, upper/lower eyelid threshold maps, and optional pupil maps.
- `uncannyEyes/user*.cpp` contains optional auxiliary hardware/user behavior. The default `user.cpp` is an empty hook.

## Eye data and rendering

Each eye design supplies:

- `sclera`: RGB565 source image, with dimensions ranging from 128 to 200 pixels depending on design.
- `iris`: RGB565 polar-style iris image, normally 256 or 512 pixels wide and 64 or 80 pixels high.
- `upper` and `lower`: 128x128 8-bit eyelid threshold maps. Both asymmetrical and symmetrical upper/lower variants are generated so the same renderer can support left/right-specific lids.
- `polar`: 80x80, 128x128, 160x160, or 180x180 lookup table encoding iris angle and radial distance.

`drawEye()` in `uncannyEyes.ino` is the core renderer. For each 128x128 output pixel it:

1. Uses the eyelid threshold maps to decide whether the pixel is covered.
2. Uses the sclera image when the current iris sample is outside the iris.
3. Uses `polar` to convert screen coordinates into iris angle/radius coordinates.
4. Scales the radius using `iScale`, then samples the iris image.
5. Emits an RGB565 value to the selected display.

The renderer therefore depends on the generated tables but not on a particular PNG loader or desktop graphics API.

## Animation behavior

`frame()` owns the original behavior:

- Autonomous gaze targets are selected inside a circle and reached with the `ease[]` curve.
- Motion and hold durations are randomized.
- Autonomous blinking uses closing and opening states, with a slower opening phase.
- Optional joystick, blink/wink inputs, and light/iris inputs are supported.
- `TRACKING` samples the upper lid map to make the lid follow the pupil.
- Blink thresholds are combined with tracking thresholds.
- Iris size follows either a sensor input or a recursive randomized motion path.
- With two eyes, the second eye's crop coordinate is mirrored before rendering and its display copy is horizontally flipped, preserving matching lateral gaze direction and the intended eye anatomy.

The original `frame()` updates one eye per call and alternates eyes. The desktop port should preserve the state transitions and timing while allowing both eyes to be composed into one desktop frame.

## Hardware-specific boundary

The following portions are tied to microcontroller/display hardware and must not be carried into the desktop build:

- `SPI.h`, `Adafruit_GFX.h`, display-driver includes, and board-specific display classes.
- SPI transactions, chip-select pins, data/command pins, reset/backlight pins, and display initialization.
- `Adafruit_ZeroDMA`, DMA descriptors, scanline DMA buffers, and Teensy SPI FIFO writes.
- `Wire.h`, `SYNCPIN`, and I2C synchronization between separate boards.
- `Serial`, `pinMode`, `digitalRead`, `digitalWrite`, `analogRead`, `delay`, `millis`, and `micros`.
- `eyeInfo[]` display-select and wink-pin entries.
- Board conditionals for Teensy, SAMD, nRF52, Hallowing, Circuit Playground, and Trinket.
- Startup logo handling and display rotation/mirroring register writes.

The optional `user_bat.cpp` and `user_xmas.cpp` files also depend on capacitive-touch, servo, and NeoPixel hardware and are outside the desktop rendering path.

## Resolution assumptions

The eye animation and generated lid maps are fixed at `SCREEN_WIDTH == 128` and `SCREEN_HEIGHT == 128`. The sclera and iris source canvases are larger and are sampled with offsets derived from the fixed screen size. The upstream display path writes exactly one 128x128 frame per eye, except for the Circuit Playground 240x240 path, which uses 2x2 pixel doubling and crops four pixels around the perimeter.

The desktop renderer should retain the 128x128 logical eye render as the source of truth, then scale and place two non-overlapping eye regions in the selected desktop resolution while preserving aspect ratio.

## Reuse strategy

The desktop implementation should reuse the upstream generated eye tables and the mathematical rendering/animation behavior. It should replace only the display-output layer with a native Linux composition layer. SDL2 is available in the environment and provides a lightweight window/fullscreen abstraction, event handling, and a software-rendered or accelerated 2D surface suitable for the CPU-only target.
