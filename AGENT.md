We want to create a **desktop/Linux version of Adafruit's Uncanny Eyes** that displays the eyes fullscreen on a normal monitor or TV.

Repository:
https://github.com/adafruit/Uncanny_Eyes.git

## Goal

Port the existing Uncanny Eyes rendering/animation from the Arduino-targeted project to a desktop application that runs natively on Ubuntu/Linux.

The desktop application must:

* Run WITHOUT an ESP32, Arduino, Teensy, or other microcontroller.
* Run WITHOUT the original small SPI TFT/OLED displays.
* Render the eyes directly to the Linux desktop.
* Support fullscreen operation so the computer can drive a TV/monitor over HDMI or DisplayPort.
* Preserve the visual appearance and behavior of the original Adafruit Uncanny Eyes as closely as practical.
* Reuse the original eye artwork/data and rendering logic wherever practical rather than redesigning the eyes.
* Run acceptably on a CPU-only machine; do not assume CUDA, OpenCL, or a dedicated GPU.
* Prefer a simple, maintainable implementation over introducing a large framework unnecessarily.

## First: inspect before modifying

Clone/inspect the repository and determine:

1. Which source files contain the actual eye artwork/data.
2. Which code performs the eye rendering.
3. Which code controls pupil movement, blinking, eyelids, gaze, randomness, etc.
4. Which parts are specifically tied to Arduino/Teensy hardware.
5. Which parts can be reused directly or adapted.
6. Whether the existing renderer assumes a fixed display resolution.

Do NOT immediately rewrite the eye renderer.

Document the architecture you find before making major changes.

## Desktop rendering target

Choose an appropriate Linux desktop rendering approach based on the existing code.

A simple 2D renderer is preferred. SDL2, SFML, Cairo, OpenGL, or another lightweight solution is acceptable if justified by the codebase.

The application should be capable of:

* Windowed mode during development.
* Fullscreen mode for the TV.
* Detecting the available desktop/display resolution.
* Scaling the original eye graphics cleanly to the output resolution.
* Maintaining the original aspect ratio.
* Running at a reasonable animation frame rate without excessive CPU usage.

The TV should effectively just see the desktop application as a fullscreen video source.

## Display layout

Initially reproduce the normal two-eye Uncanny Eyes arrangement:

LEFT EYE                         RIGHT EYE

Each eye must have its own independent rendering state.

Do not allow the two eye images to overlap or touch unless the original design requires it.

The eyes should scale with the display rather than remaining tiny at the original hardware resolution.

## Preserve the original behavior

Where present in the repository, retain/adapt:

* Pupil movement
* Iris rendering
* Eye movement
* Random gaze behavior
* Blinking
* Eyelids
* Eye asymmetry
* Timing/randomization
* Different available eye designs
* Any existing configuration options

Do not simplify the animation merely to get something visible on screen.

## Hardware abstraction

Separate the project into two conceptual layers:

1. Eye/animation/rendering logic
2. Desktop display/output

The desktop renderer should replace the Arduino display-output layer.

Do not leave unnecessary Arduino/SPI/display-driver dependencies in the desktop build.

The final desktop program should not require Arduino IDE or an ESP board to compile or run.

## Configuration

Provide an easy configuration mechanism for at least:

* Windowed/fullscreen
* Display resolution or automatic native resolution
* Eye type/design if the original project supports multiple designs
* Scaling
* Background color
* Animation/frame-rate settings where useful

Do not bury these values throughout the source code.

## TV use

Make fullscreen mode practical for a TV.

Ideally provide something equivalent to:

```
./uncanny-eyes --fullscreen
```

and allow exiting fullscreen cleanly with ESC or another obvious key.

Do not make fullscreen the only mode during development.

## Important

Do NOT replace the Adafruit eyes with generic cartoon eyes.

The objective is specifically:

"Adafruit Uncanny Eyes running on a Linux desktop/TV instead of an ESP + TFT displays."

Use the repository's existing artwork, eye data, and behavior as the source of truth.

## Build/run

At the end, provide:

1. Exact build dependencies.
2. Exact build command.
3. Exact run command.
4. Fullscreen run command.
5. Explanation of which original files/code were reused.
6. Explanation of which Arduino-specific portions were replaced.
7. Any limitations discovered during the port.

Also verify that the resulting application actually launches and renders both eyes before declaring the task complete.

If a dependency or implementation choice is uncertain, inspect the repository and the existing code first rather than guessing.
