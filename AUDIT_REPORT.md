# Audit Report — Uncanny Eyes Desktop Port + LDR Build

## Project Overview

The project is a C++ SDL2 port of the Adafruit Uncanny Eyes animated eyes. It runs on the desktop (Linux TV) and receives LDR (light-dependent resistor) sensor data over serial from an ESP32 (Wemos D1 R32) to drive pupil dilation dynamically. The sister project at `/home/jason/suspect/suspect.ino` is an ESP8266-based 6-DOF arm controller that also reads an LDR and outputs `LDR:<raw>` serial data — that is the intended sensor firmware for this first ESP32 build.

The application compiles and runs. The primary defect is **rapid/instantaneous blinking** when the app is running in Normal mode with autonomous blink enabled.

---

## Bug Analysis: Rapid Blinking

### Root Cause: `blink()` is called unconditionally every frame in Normal mode

The bug is in `src/eye.cpp`, lines 249–251, inside `EyeAnimator::update()`:

```cpp
// eye.cpp line 249-251
} else if (settings_.autonomousBlink || blinkState_.state != kNoBlink) {
    blink(nowUs);
}
```

The condition `settings_.autonomousBlink` is `true` by default (set in `AnimationSettings` in `eye.h` line 85, and passed as `{true, true, true, 1}` in `main.cpp` line 290 and 523). When this is true, `blink(nowUs)` is called on **every single frame** (60 FPS by default).

Now examine `blink()` (line 179–186):

```cpp
void EyeAnimator::blink(std::uint64_t nowUs) {
    if (blinkState_.state != kNoBlink) {
        return;  // Already blinking — return early
    }
    blinkState_.state = kClosing;
    blinkState_.startTimeUs = nowUs;
    blinkState_.durationUs = static_cast<std::uint64_t>(random(60'000, 140'001));
}
```

**The logic is correct in isolation** — `blink()` guards with `if (blinkState_.state != kNoBlink) return;`. So when a blink is in progress, it returns immediately. The problem is what happens when the blink **completes**:

At line 424–432, when the blink finishes (elapsed >= duration):
```cpp
if (elapsed >= blinkState_.durationUs) {
    if (blinkState_.state == kClosing) {
        blinkState_.state = kOpening;
        blinkState_.durationUs *= 2;
        blinkState_.startTimeUs = nowUs;
    } else {
        blinkState_.state = kNoBlink;       // ← Back to no blink
        blinkState_.startTimeUs = 0;
        blinkState_.durationUs = 0;
    }
}
```

When the opening phase completes, `state_` is set to `kNoBlink`. On the **very next frame**, the `else if` condition on line 249 is still true (`settings_.autonomousBlink == true`), so `blink(nowUs)` is called again immediately. Since `state_` is now `kNoBlink`, the guard in `blink()` doesn't trigger, and a new blink starts with `nowUs` as the start time.

**Result:** There is no gap between blinks. The eyes close (60–140ms), open (120–280ms, since opening duration = closing duration × 2), and immediately re-close. This produces the **rapid, continuous blinking** the user observes.

### Comparison with Upstream (Adafruit `uncannyEyes.ino`)

The upstream code (lines 533–635 of `Uncanny_Eyes/uncannyEyes/uncannyEyes.ino`) implements autonomous blink correctly:

```cpp
// Upstream autoreblink — only triggers when time interval elapses
if((t - timeOfLastBlink) >= timeToNextBlink) {
    timeOfLastBlink = t;
    uint32_t blinkDuration = random(36000, 72000);
    // ...
    timeToNextBlink = blinkDuration * 3 + random(4000000);
    // 36000*3 + 4000000 = ~4.5–5M us = ~4.5–5 seconds between blinks
}
```

The upstream code has a `timeToNextBlink` interval of **3–8 seconds** between blinks (matching `DrowsinessConfig.blinkIntervalMin/Max` of 3–8 seconds in the DrowsinessModel). The desktop port's `blink()` is called every frame with no interval gating, so there is effectively **zero delay** between the end of one blink and the start of the next.

### The Unused `nextBlinkUs_` / `lastBlinkUs_` Fields

The `EyeAnimator` class declares two fields that are never used:

```cpp
// eye.h lines 152-153
std::uint64_t lastBlinkUs_ = 0;
std::uint64_t nextBlinkUs_ = 4'000'000;
```

These appear to be **leftover scaffolding** from the original design intent — they were meant to implement the interval-gating that the upstream code uses (random 3–8 second gap between blinks). Either they were never wired up, or they were connected and then the wiring was removed/accidentally deleted. The `blink()` function should check `nowUs >= nextBlinkUs_` before initiating a new blink, and should reschedule `nextBlinkUs_` with a random interval (matching upstream's `blinkDuration * 3 + random(4'000'000)` pattern, which yields 3.1–7.3 seconds).

### Additional Blink Duration Issue

The desktop port's blink duration is `random(60'000, 140'001)` microseconds = **60–140ms closing**. The upstream uses `random(36000, 72000)` = 36–72ms closing. The desktop values are roughly 2× longer, but this alone would not cause rapid blinking — it would make blinks slower. The rapid-blink symptom is solely due to the missing interval gate.

### Why the User Said "Float Called 2x or Similar"

The user's hypothesis was "a float/discard being called 2x." The actual issue is structurally similar: **`blink()` is called every frame** (60 times/second), and when the blink state returns to `kNoBlink`, it immediately starts a new blink on the next frame. There is no "float" being called twice — but there is an **event being triggered twice** (the same logical event — start a new blink — fires every frame instead of once per interval). The pattern matches the user's intuition about something being called 2x or with no gating.

### Fix

Add interval-gating to the `blink()` call. Two approaches:

**Approach A (minimal, matches upstream):** Use the existing `lastBlinkUs_` and `nextBlinkUs_` fields:
```cpp
// In EyeAnimator::update(), replace the autonomous blink call:
} else if (settings_.autonomousBlink || blinkState_.state != kNoBlink) {
    if (blinkState_.state == kNoBlink && nowUs >= nextBlinkUs_) {
        blink(nowUs);
        nextBlinkUs_ = nowUs + static_cast<std::uint64_t>(
            random(3'000'000, 8'000'001));  // 3-8 second interval
    } else if (blinkState_.state != kNoBlink) {
        // Still in progress — let blink() / state machine handle it
        // Actually, don't call blink() here at all; state machine runs on its own
    }
}
```

**Approach B (cleaner):** Move the interval logic into `blink()` itself, or add a separate `scheduleBlink`/`shouldBlink` method. The `blink()` method should only be called to *initiate* a blink, not to *advance* an in-progress blink (the state machine logic in `update()` at lines 407–434 already handles advancement).

**Recommended fix:** The `blink()` call on line 250 should **only trigger when `state_ == kNoBlink` AND the interval has elapsed**. When a blink is in progress, the state machine at lines 407–434 handles progression without needing `blink()` to be called.

---

## LDR Serial Protocol Mismatch

There is a **critical protocol mismatch** between the reference ESP32 LDR sketch and the app's parser.

### `reference/ldr_esp32/ldr_esp32.ino`:
```cpp
void loop() {
    int value = analogRead(LDR_PIN);
    Serial.println(value);       // ← Outputs raw number: "2048\n"
    delay(50);
}
```
This outputs **bare integers** (e.g., `2048`), not `LDR:2048`.

### `serial_monitor.py` (line 26):
```python
m = re.match(r'LDR:(\d+)', line)
```
Expects the `LDR:` prefix.

### `LDRSerialInput::parseLine()` (line 114–136):
```cpp
const std::string prefix = "LDR:";
const bool hasPrefix = line.compare(0, prefix.size(), prefix) == 0;
const std::size_t firstDigit = hasPrefix ? prefix.size() : 0;
```
Accepts both `LDR:2048` and bare `2048` (it strips the prefix if present, otherwise starts from 0).

### Sister project `suspect.ino` (line 743–744):
```cpp
Serial.print("LDR:");
Serial.println(raw);   // ← Correct: "LDR:2048\n"
```

### Analysis:
- `suspect.ino` (ESP8266) outputs `LDR:<raw>` — **compatible** with the desktop app.
- `reference/ldr_esp32/ldr_esp32.ino` outputs bare `<raw>` — `parseLine()` will actually **accept this** (it handles both prefixed and unprefixed), so the app will work with the ESP32 reference. However, `serial_monitor.py` will **not** parse bare numbers — it requires the `LDR:` prefix.
- The `LDR_DEBUG` debug output in `suspect.ino` (lines 700–744) will **flood the serial port** with `LDR_DEBUG:...` and `LDR:...` lines at 115200 baud every 5000ms, which is fine. But note that `suspect.ino` is the ESP8266 arm controller, **not** an LDR sensor node. The user said this is the "first ESP32 implementation build," but `suspect.ino` is ESP8266 — the ESP32 reference sketch (`ldr_esp32.ino`) is the one that would run on the Wemos D1 R32.

### Key Issue: `ldr_esp32.ino` does not output `LDR:` prefix
If the user deploys `reference/ldr_esp32/ldr_esp32.ino` to the Wemos D1 R32 as-is, the app's `parseLine()` will parse bare numbers OK, but `serial_monitor.py` will show them as raw non-matching lines. **Recommendation:** Update `ldr_esp32.ino` to output `LDR:<value>` to match `suspect.ino` and `serial_monitor.py`'s expectations. Also apply the lux calculation from `suspect.ino` (voltage divider formula with pulldown resistance) rather than outputting raw ADC.

### Additional Issue: `ldr_esp32.ino` has no `LDR_DEBUG` line
The `suspect.ino` outputs both `LDR_DEBUG:raw=...` (debug) and `LDR:...` (data) lines. The `ldr_esp32.ino` reference only outputs the raw value. If both debug and data lines flow simultaneously, `parseLine()` will receive `LDR_DEBUG:...` lines, which will fail to parse (returns false, line is discarded — OK) but will also receive bare numbers which parse as values. This is inconsistent. The ESP32 sketch should be updated to match the `suspect.ino` output format.

---

## Artwork Issues (All Three Custom Dragon Variants)

### 1. All three custom dragon generated headers are effectively stubs (fully black sclera)

All three files (`customDragonEye.h`, `customDragonEye_A.h`, `customDragonEye_B.h`) begin with the sclera array entirely filled with `0X0000` (black). While later rows may contain non-zero values (the files are ~16K lines each), the first ~60 rows shown are all-zero. More critically:

### 2. `customDragonEye.h` is never included by any source file

- `artwork_custom_dragon.cpp` includes `"generated/customDragonEye_A.h"` (line 3)
- `artwork_custom_dragon_A.cpp` includes `"generated/customDragonEye_A.h"` (line 3)
- `artwork_custom_dragon_B.cpp` includes `"generated/customDragonEye_B.h"` (line 3)
- **No file includes `"generated/customDragonEye.h"`**

The file `customDragonEye.h` (1,165,208 bytes) is **dead code** — it is never referenced. It should be quarantined.

### 3. All three custom dragon variants use `IRIS_MIN 80`, `IRIS_MAX 400` (non-default)

The default artwork uses `irisMin=120, irisMax=720` (set explicitly in `artwork_default.cpp`). The upstream dragon artwork uses its own `IRIS_MIN`/`IRIS_MAX` from the graphics header. The custom dragon headers override these to `80`/`400`, which changes the iris scale range. This may be intentional (smaller iris range for the dragon style) but is inconsistent with the default eye and should be verified.

### 4. PASSDOWN.md confirms smoke-test failure

Per `PASSDOWN.md`: the custom dragon variants "fail the smoke test because their generated sclera/iris conversion contains no red pixels." This means the iris artwork has no red channel data — the eyes will render with incorrect/missing iris texture coloring.

---

## TUI Bug: References Nonexistent `customDragonC`

`uncanny-tui.py` line 8:
```python
DESIGNS = ["default","cat","dragon","customDragon","customDragonA","customDragonB","customDragonC"]
```

There is **no `customDragonC`** design:
- `artwork_registry.cpp` (line 7–11) registers only 6 designs: `default`, `cat`, `dragon`, `customDragon`, `customDragonA`, `customDragonB`.
- `artwork.h` declares only 6 functions (no `customDragonCArtwork()`).
- `CMakeLists.txt` (line 19–21) compiles only 3 custom dragon source files (no `artwork_custom_dragon_C.cpp`).
- No `customDragonEye_C.h` exists in `src/generated/`.

If the user selects design 7 in the TUI (index 6 = `customDragonC`), the app will crash with "unknown eye design: customDragonC" because `findArtwork()` (main.cpp line 266–273) throws `std::runtime_error`.

### The `customDragonEye.h` file — possible intended `customDragonC` source?

The orphaned `customDragonEye.h` (not `_A` or `_B`) could have been intended as the source for a `customDragonC` variant, but no `artwork_custom_dragon_C.cpp` or `customDragonEye_C.h` was created. The naming scheme is inconsistent: `customDragonEye_A.h`/`_B.h` vs `customDragonEye.h` (no suffix).

---

## Build System Issues

### Two redundant build directories

Both `build-ldr/` and `build/` exist with separate CMake caches:
- `build-ldr/` — uses Make, binary timestamp Sep 23 03:13
- `build/` — uses Ninja (has `build.ninja`), binary timestamp Sep 24 05:07 (newer)

This is confusing and redundant. One should be removed. The TUI and `debug_run.sh` reference `build-ldr/uncanny-eyes`, so that is the canonical one — but `build/` has a newer binary.

### `tui_debug.sh` has a typo

Line 7: `[ -f build-ldr/uncanny-yes ]` — should be `uncanny-eyes`, not `uncanny-yes`. This means the binary existence check always reports "NO" even when the binary exists.

---

## Quarantine Candidates (Dead / Unused / Broken Files)

| File | Reason | Recommendation |
|------|--------|----------------|
| `src/generated/customDragonEye.h` | Never `#include`d by any source file. 1.2MB of dead data. | Quarantine / delete. |
| `build/` directory | Redundant with `build-ldr/`. | Remove or pick one canonical build dir. |
| `351dm3rhjd861.jpg` | JPEG image (2500×2500) in project root, no reference. | Quarantine — unidentified asset. |
| `eye_conversion/clipart_iris_source.png` | 900×507 PNG, no reference in source. | Quarantine. |
| `appimage/` and `dist/uncanny-eyes-portable/` | Full AppImage distribution (binary + libs + desktop file). Same binary (identical BuildID). | Verify they are release artifacts, not build inputs. Quarantine if redundant. |
| `reference/ldr_esp32/ldr_esp32.ino` | Minimal stub that outputs bare ADC values without `LDR:` prefix. Incompatible with `serial_monitor.py`. | Flag for update to match `suspect.ino` output format. |
| `tui_debug.sh` | Has typo (`uncanny-yes` vs `uncanny-eyes`), redundant with `debug_run.sh`. | Fix or remove. |
| `docs/AGENT.md` (in suspect project) | Contains UI/feature requests unrelated to current scope. | Out of scope — leave as-is. |
| `Uncanny_Eyes/uncannyEyes/user.cpp` and `user.h` | User hooks (likely empty placeholders from upstream). Verify. | Check if empty/referenced. |

---

## LDR Serial Integration Assessment

### `LDRSerialInput.cpp` — solid implementation

The serial input thread, reconnection logic, filtering (EMA with alpha), normalization, and timeout handling are all well-implemented. The `parseLine()` method correctly handles both `LDR:NNN` and bare `NNN` formats.

### `serial_monitor.py` — simple but limited

Only parses `LDR:(\d+)` lines. Does not use the lux calculation that `suspect.ino` performs. It computes a simple `raw * 1000 / 1023` for lux and a linear pupil mapping. This is a monitoring tool, not the real integration path — the real path is `LDRSerialInput` → `setEnvironmentalPupil()`.

### `PupilResponseModel.cpp` — unused

The `PupilResponseModel` class is compiled into `uncanny-eyes-ldr-test` (CMakeLists.txt line 33) but is **never instantiated** in `main.cpp`. The main app calls `animator.setEnvironmentalPupil()` directly from `LDRSerialInput::snapshot()`. The `PupilResponseModel` appears to be an abandoned abstraction or a test-only helper. The test binary `tests/ldr_test.cpp` likely uses it.

---

## Summary of Issues by Severity

### Critical
1. **Rapid blinking** — `blink()` called every frame with no interval gating. The `nextBlinkUs_`/`lastBlinkUs_` fields exist but are never used. **This is the primary bug.**
2. **`customDragonC` in TUI** — references a non-existent design; selecting it will crash the app.
3. **ESP32 LDR reference sketch** (`ldr_esp32.ino`) outputs bare numbers without `LDR:` prefix; `serial_monitor.py` cannot parse them.

### High
4. **Three custom dragon artwork variants are stubs** — sclera arrays start all-black; PASSDOWN.md confirms no red pixels in iris. Smoke test would fail.
5. **`customDragonEye.h` is dead code** — never included, 1.2MB waste.
6. **`tui_debug.sh` typo** — `uncanny-yes` instead of `uncanny-eyes`.

### Medium
7. **Two redundant build directories** (`build/` and `build-ldr/`).
8. **Redundant AppImage dist** — binary is identical (same BuildID) in both `appimage/` and `dist/`.
9. **`PupilResponseModel` compiled but never used** in the main app.
10. **Unidentified assets** — `351dm3rhjd861.jpg`, `clipart_iris_source.png` in wrong locations.

### Low
11. Inconsistent iris range for custom dragon (80/400 vs default 120/720).
12. `suspect.ino` outputs excessive `LDR_DEBUG:` lines that could interfere with serial parsing if both lines are present on the same port simultaneously (though `parseLine()` correctly rejects non-`LDR` lines).

---

## Modernization Suggestions

1. **Fix blink interval gating** — wire up `nextBlinkUs_` and `lastBlinkUs_` in the blink logic to implement the 3–8 second interval from upstream. Use the `DrowsinessConfig` blink interval parameters when in Dozing mode.
2. **Remove `customDragonC`** from the TUI `DESIGNS` list, or implement the missing artwork.
3. **Delete `customDragonEye.h`** (dead code) or rename to `customDragonEye_C.h` and implement `artwork_custom_dragon_C.cpp` if the third variant is desired.
4. **Consolidate build directories** — keep `build-ldr/` as canonical (TUI references it), remove `build/`.
5. **Update `ldr_esp32.ino`** to output `LDR:<raw>\n` format and optionally compute lux using the voltage-divider formula from `suspect.ino`.
6. **Fix `tui_debug.sh` typo** or remove the file (it duplicates `debug_run.sh`).
7. **Re-examine custom dragon artwork conversion** — the iris PNGs exist in `Uncanny_Eyes/convert/customDragon/` but the generated headers appear to contain stub/placeholder data. Re-run the conversion pipeline (`tablegen.py` / `hextable.py`).
8. **Remove `PupilResponseModel`** from the main build if unused, or wire it into the main app's pupil response path for consistency.
9. **Add a `.gitignore`** to prevent build artifacts and binaries from being committed.
