# Build and run

## Dependencies

- CMake 3.16 or newer
- A C++17 compiler
- SDL2 development files

On Ubuntu/Debian:

```sh
sudo apt-get install cmake g++ libsdl2-dev
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run windowed

```sh
./build/uncanny-eyes
```

The default design is the upstream multicolor `defaultEye` artwork. Use the flat cartoon cat artwork, the original dragon artwork, or the custom dragon artwork explicitly with:

```sh
./build/uncanny-eyes --design cat
./build/uncanny-eyes --design dragon --mode dozing
./build/uncanny-eyes --design customDragon --mode dozing
```

Dozing dragon mode uses a slow shared breathing cycle, mostly closed eyelids, subtle gaze drift, and synchronized blinks.

### Eye positioning

Eyes are positioned in the upper portion of the window using two parameters
in `src/main.cpp`:

1. **`kEyeHeightFraction`** (default `0.72F`): Controls eye size as a fraction
   of window height. Larger = bigger eyes.
2. **`eyeVerticalBias`** (default `0.40F`): Positions eyes higher than center.
   `0.50F` = vertically centered, `0.40F` = shifted up by 8% of window height.

### Borderless window

For RTSP streaming, use `--borderless` to remove window decorations:

```sh
./build/uncanny-eyes --width 1280 --height 720 --borderless --background 0,0,0
```

## Run fullscreen

```sh
./build/uncanny-eyes --fullscreen
```

Press `Esc` to exit. Press `F` to toggle fullscreen while running. Press `D` to toggle dozing mode.

Useful options:

```text
--width PIXELS --height PIXELS --scale FACTOR --fps RATE
--background R,G,B --design NAME --mode MODE --smoke-test --list-designs
```

Available designs: `default`, `cat`, `dragon`, `customDragon`

The smoke test renders three frames for both eyes and verifies that red, green, and blue channels are present:

```sh
./build/uncanny-eyes --smoke-test
```

## USB serial LDR input

An optional light-dependent resistor (LDR) can drive iris size over a USB serial
device. The feature is **disabled by default**; without `--ldr` the application
behaves exactly as before.

```sh
./build/uncanny-eyes --ldr --ldr-port /dev/ttyUSB1
```

The reader runs on a background thread and never blocks the render loop. Each
line must contain one non-negative integer, optionally prefixed with `LDR:`.
Malformed, empty, trailing-garbage, and over-long lines are ignored.

```sh
./build/uncanny-eyes --ldr --ldr-port /dev/ttyUSB1 --ldr-baud 115200 \
  --ldr-min 0 --ldr-max 4095 --ldr-invert \
  --ldr-alpha 0.15 --ldr-min-pupil 0.25 --ldr-max-pupil 1.0 \
  --ldr-constriction 0.35 --ldr-dilation 0.80 --ldr-timeout 2000 \
  --ldr-curve 0.45 --ldr-iris-weight 0.75
```

Calibration notes:

- `--ldr-min` / `--ldr-max` are the ADC range used to normalize the reading.
- `--ldr-invert` reverses the normalized direction if your sensor is wired
  so that bright light lowers the ADC value.
- `--ldr-alpha` is the low-pass filter weight; higher values track faster but
  are noisier.
- Bright light maps to `--ldr-min-pupil` and dark light to
  `--ldr-max-pupil`. Constriction (bright response) is faster than dilation
  (dark response) by default.
- The target pupil follows a nonlinear power curve (`--ldr-curve`) and an
  exponential response toward the new target.
- While the device is connected but silent, the last pupil state is held for
  `--ldr-timeout` milliseconds, then the environmental modifier stops and the
  eyes return to autonomous iris. Reconnect attempts continue unless
  `--ldr-no-reconnect` is set.
- Press `L` while running to print connection state, raw, filtered,
  normalized, and pupil values.

### Linux permissions

The serial device is owned by `root:dialout` with mode `0660`. Add your user
to the `dialout` group or run the application with `sudo`:

```sh
sudo usermod -aG dialout "$USER"
```

### Reference sketch

`reference/ldr_esp32/ldr_esp32.ino` is a minimal ESP32 sketch that samples an
analog LDR pin and prints one integer per line at 115200 baud:

```cpp
const int LDR_PIN = 34;

void setup() {
    Serial.begin(115200);
}

void loop() {
    int value = analogRead(LDR_PIN);
    Serial.println(value);
    delay(50);
}
```

## Remote Display (RTSP Streaming)

The uncanny eyes can be streamed to a remote device (e.g., TV, phone) over RTSP
using the Python RTSP server and GStreamer. This is useful for displaying the
eyes on a Google TV or any device running VLC.

### Prerequisites

- GStreamer core + RTSP server plugin
- Python 3 with GObject introspection (`python3-gi`)

```sh
sudo apt install -y gstreamer1.0-rtsp libgstrtspserver-1.0-0 \
  gir1.2-gst-rtsp-server-1.0 python3-gi
```

### Start the stream

```sh
./run_rtsp.sh
```

Or manually:

```sh
# 1. Start the eyes app (windowed, 1280x720)
DISPLAY=:0 ./build/uncanny-eyes --width 1280 --height 720 --background 0,0,0 &

# 2. Start the RTSP server (auto-detects the eyes window)
DISPLAY=:0 python3 rtsp_server.py
```

The server auto-detects the uncanny eyes window on the X11 display and captures
**only that window**, not the full screen. This is important for multi-monitor
setups (e.g., stacked displays). The `ximagesrc` element uses the window's XID
(`xid` property) to capture exactly the eye window contents.

### View on VLC

On the remote device (e.g., Google TV):

1. Open VLC
2. Network Stream → `rtsp://10.0.0.147:8554/eyes`
3. The stream is 1280x720 @ 30fps H.264

### Manual window ID

If auto-detection fails, pass the window ID as argument:

```sh
# Find the window ID
DISPLAY=:0 xwininfo -root -tree -int | grep -i uncanny | grep -v mutter

# Start with the specific window ID
DISPLAY=:0 python3 rtsp_server.py 50331658
```

### How it works

- `ximagesrc` with `xid` property captures **only** the specified X11 window
- `videoscale` forces exact 1280x720 output (H.264 requires even dimensions)
- `x264enc` encodes to H.264 with zero-latency tuning
- `rtph264pay` packages as RTP for RTSP transport
- The GStreamer RTSP server (`GstRtspServer`) serves on port 8554, mount `/eyes`

## TUI Controller (uncanny-tui.py)

The TUI provides an interactive menu for launching and controlling the eyes application.

```sh
python3 uncanny-tui.py
```

Commands:
- `[1-6]` Change design
- `[m]` Toggle mode (normal/dozing)
- `[f]` Toggle fullscreen
- `[l]` Toggle LDR sensor
- `[t]` Toggle RTSP stream (launches rtsp_server.py)
- `[s]` Start process
- `[x]` Stop process
- `[r]` Restart process
- `[q]` Quit

Press `t` to launch the RTSP stream server. The VLC URL will be displayed.

## Portable Distribution

A portable tarball is available at `dist/uncanny-eyes-portable.tar.gz` containing a self-contained build with all four eye designs. Extract and run with:

```sh
tar -xzf dist/uncanny-eyes-portable.tar.gz
./uncanny-eyes-portable/run.sh --design customDragon --mode dozing --fullscreen
```
