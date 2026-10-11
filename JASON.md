# JASON.md - Comprehensive Session Work Summary

## Session Overview

This session involved extensive refactoring and feature additions to the Uncanny Eyes project, including architectural changes to the rendering/animation system, calibration models, UI updates, and asset handling.

## Summary of Changes by File

### Core Rendering & Animation

#### src/eye.cpp (928 changes: +697/-231)
Major architectural refactoring including:
- Added calibrated pupil-to-limbus ratio model with `dilationToPupilRatio()`, `pupilRadiusFromCalibration()`, `dilationFromIrisScale()` functions
- Implemented `PupilGeometry` calculation with calibrated radius logic
- Added `smoothStep()` and `cosFade()` utility functions for smoother transitions
- Rewrote renderer to use `RenderInput` contract (mode-blind architecture) - separating rendering from animation logic
- Added medial caruncle fade with cosine falloff for rounded corners (default eye only)
- Implemented gaze clamping to preserve medial caruncle anchor (maxGazeDeviation clamping)
- Refactored animator to use `RenderInput`, `AnimationSettings`, `EyeState` structures
- Added support for expression profiles with fixation strength
- Modified eyelid closure logic - both lids close by raising thresholds
- Removed head roll rotation from rendering path (no head in display)
- Added `rebuildProfile()` and enhanced animation state management

#### src/eye.h (255 changes: +208/-47)
- Added `EyeCalibration` struct for normalized ratio model
- Added `RenderInput` struct as pure render contract (separates behavior from rendering)
- Added `EyeState`, `GazeMotion`, `BlinkState`, `EyeFrame` structures
- Added `PupilGeometry` struct for shared pupil geometry
- Enhanced `EyeArtwork` with calibration and pupil shape fields
- Added `RenderSettings` struct
- Added `AnimationSettings` with more configuration options
- Added `ExpressionProfile` integration fields
- Added alertness/drowsiness model includes and method declarations

#### src/main.cpp (615 changes: +452/-163)
- Added extensive CLI options: stream-mode, autonomous-iris, width/height, iris-type, pupil-type, realism, expression-intensity, ldr-sensitivity, scale, background, fps, face-overlay, drowsiness config, etc.
- Added vocabulary listing flags (--list-modes, --list-iris-types, --list-pupil-types, --list-realism)
- Implemented `RenderInput`-based rendering pipeline
- Added `applyEyeTypes()` function
- Added comprehensive smoke tests for all modes
- Removed head roll rotation from draw path
- Updated stream mode to 1080p default
- Added face overlay rendering support

### UI & Control

#### uncanny-tui.py (505 changes: +427/-78)
- Complete rewrite from simplified TUI to full-featured controller
- Discovers designs from binary via --list-designs (no hardcoded drift)
- Discovers vocabularies (modes, iris types, pupil types, realism) from binary
- Added full option control: iris-type, pupil-type (with override toggle), realism, debug iris cycle, expression intensity, ldr sensitivity
- Added LDR calibration controls (min-pupil, max-pupil, curve, invert)
- Added face overlay toggle, stream mode toggle
- Added tuning parameters display
- Enhanced status display with process/stream state
- Auto-detects LDR serial port
- Manages RTSP stream via external script

### Models & Calibration

#### src/PupilResponseModel.cpp (126 changes: +111/-15)
- Enhanced with nonlinear curve support (power curve for light response)
- Added constriction/dilation time constants
- Implemented proper physiological response modeling
- Added sigmoid approximation option

#### src/PupilResponseModel.h (68 changes: +64/-4)
- Added `PupilResponseConfig` with curve, min/max pupil fractions, constriction/dilation times
- Added `environmentalTarget` support
- Added nonlinear response methods

#### src/LDRSerialInput.cpp (52 changes: +45/-7)
- Enhanced LDR input with filtering and validation
- Improved reconnection logic
- Added timeout handling

#### src/LDRSerialInput.h (5 changes: +4/-1)
- Extended configuration options

### Assets & Generation

#### create/normalize_eye.py (46 changes: +37/-9)
- Added support for "no-sclera" mode when sclera not visible
- Improved limbus detection with fallback
- Adjusted IRIS_SIZE calculation logic
- Fixed eyeball framing logic

#### create/EYE_ASSET_SPEC.md (102 changes: +89/-13)
- Comprehensive specification for eye asset generation
- Documented lid maps, iris unrolling, pupil geometry
- Added detailed examples and conventions

#### src/artwork_*.cpp files
- Updated multiple artwork files (cat, chalk_blue, cold_blue, custom_dragon variants, default, dragon, fire) to include calibration data
- fire: added slit pupil geometry (vertical slit, ratio 0.26)
- chalk_blue: round pupil default
- Added pupilGeometry calibration in artwork structs

#### src/artwork.h (15 changes: +14/-1)
- Added chalkBlueEyeArtwork declaration
- Added EyeCalibration includes

#### src/artwork_registry.cpp (7 changes: +5/-2)
- Added chalkBlue to artwork registry
- Reordered/updated registry entries

### Tests

#### tests/ldr_test.cpp (383 changes: +303/-80)
- Expanded LDR input tests
- Added filtering and reconnection tests

#### tests/pupil_ratio_test.cpp (NEW)
- New test for pupil-to-limbus ratio calibration model
- Tests dilation clamping, cross-eye proportionality, monotonicity

### Build & Documentation

#### CMakeLists.txt (62 changes: +52/-10)
- Added pupil_ratio_test executable and test
- Added artwork_chalk_blue.cpp to all targets
- Updated build configuration

#### BUILDPLAN.md (394 changes: +272/-122)
- Comprehensive build plan documentation

#### STATE.md, TODO.md, README.md
- Updated project state and documentation
- README: updated defaults (1920x1080, 1080p stream), added new CLI options

#### rtsp_server.py (8 changes: +5/-3)
- Minor RTSP server updates

### Untracked/New Files
- src/generated/*.h files for new variants (bluehole, cham, coldBorg, coldborgjr, nightBloom, noSclera, nptest, sgtpkldck, testy, xwhole, yolk, zwhole)
- tests/pupil_ratio_test.cpp
- tray_app.py, svg/, srp-css-theme-pack/, images (stock photo, webp)
- Uncanny_Eyes directory

### Specific Changes to Default Eye & Normal Mode

#### Default Eye (artwork_default.cpp)
- Added pupil geometry calibration fields to EyeArtwork struct:
  - `pupilShape = 0, slitRatio = 0.0F` (defaults for round pupil)
  - `EyeCalibration{100.0F, 0.12F, 0.42F, 100.0F, 100.0F}` (limbusDiameter 100, pupilRatioMin 0.12, pupilRatioMax 0.42, center at 100,100)

#### Normal Mode (ExpressionProfile.cpp)
- Normal profile gaze parameters:
  - `gazeWanderAmount = 0.15F` (was likely different; now constrained)
  - `maxGazeDeviation = 0.15F` (hard clamp on gaze deviation from center - ~15% of coordinate range)
  - `fixationStrength = 0.0F`
  - `gazeSpeedScale = 0.7F`
  - `lidDroop = 0.0F`, `lidAsymmetry = 0.0F`
  - Blink timing: 3.0-8.0s interval, 0.07-0.14s duration
  - `agitation = 0.0F`

#### Renderer Changes Affecting Default Eye
- **Caruncle fade**: Changed from conditional fade only on doze (lidFade>0) to always-on for default eye with cosine falloff
  - Old: linear fade based on caruncleDist 10px, applied only when lidFade>0
  - New: cosine (cosFade) falloff in 8px band from edges on both sides, always applied for default eye
  - Strength increases with closure: `strength = 0.10F + 0.10F * closure`
- Both left and right corners now rounded symmetrically

#### Gaze/Movement Changes
- **Gaze clamping added**: In `resolveGaze()`, gaze is clamped to maxGazeDeviation from center using profile's value
  - `clampRadius = kCoordinateMax * profile_.maxGazeDeviation` (~15% for normal)
  - This restricts how far eyes can look from center regardless of wander or dart targets
