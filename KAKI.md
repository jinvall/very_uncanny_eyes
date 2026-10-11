# Uncanny Eyes - Notes & Discussion

## Stream Configuration

### RTSP Server (Port 8554)
- **Mount point**: `/eyes`
- **Video**: H.264, 1280x720, 30fps
- **Bitrate**: 2000 kbps
- **Pipeline**: `ximagesrc` → `videoscale` → `x264enc` → `rtph264pay`

### Window Detection
The RTSP server auto-detects the uncanny eyes window using `xwininfo`. It captures only the window region, not the full desktop. This is critical for multi-monitor setups.

Known window: 1280x720 at position (320,1297) on dual-monitor setup.

### Dual-Monitor Display
- Display setup: 40-inch TV above 42-inch TV (stacked, joined X11 display)
- Window position varies: eyes window appears at (320,1297) on combined desktop
- The `videoscale` element in the pipeline forces exact 1280x720 output to satisfy H.264 even-dimension requirement

### Dependencies
```sh
sudo apt install -y gstreamer1.0-rtsp libgstrtspserver-1.0-0 \
  gir1.2-gst-rtsp-server-1.0 python3-gi
```

### Launch
```sh
./run_rtsp.sh
# or
DISPLAY=:0 python3 rtsp_server.py
```

### VLC URL
```
rtsp://10.0.0.147:8554/eyes
```

## Known Issues

1. **503 on DESCRIBE**: Pipeline fails if ximagesrc returns odd dimensions (e.g., 1281x721). Fix: `videoscale` forces 1280x720.
2. **Window detection fails**: If xwininfo doesn't find "uncanny" window, falls back to fullscreen (0,0 to 1920,1080).
3. **Port 8554 in use**: `pkill -f rtsp_server.py` then restart.
4. **App window not visible**: Ensure `DISPLAY=:0` and the app is running windowed (not fullscreen mode).

## 2026-10-10 — Head-motion and gaze-clamp removal (authorized items 1 & 2)

The display window has no head, so all head animation was removed:

- `DrowsinessModel`: head pitch/yaw/roll state, the sine-wave micro-motion
  block, the micro-sleep head drop, and the head-bob/drop/instability config are
  gone. Dozing crop translation by head yaw/pitch (previously a 4-5 px crop
  wheel measured in dozing) is gone.
- `AlertnessModel`: headRoll, headRollAmount, headJitter and their sine
  computation are gone.
- `ExpressionProfile`: headYawAmount/headPitchAmount/headRollAmount/
  headRollDegrees fields removed, along with the realism scaling of them.
- `EyeFrame::headRoll` removed; the commented-out REMOVED blocks in eye.cpp and
  main.cpp were deleted, and the dozing debug key no longer prints head pose.

Gaze clamping is now limited to the artwork bounds only:

- The `maxGazeDeviation` clamp in `resolveGaze()` and the `gazeWanderAmount`
  radius restriction in `advanceGaze()` are deleted, as is the dozing
  `wanderScale` multiplier. Measured: an explicit `setGaze(0)` now resolves to
  gazeX 0 (was 358), `setGaze(1023)` to 1023 (was 664), and autonomous gaze
  spans the legal range (was 455..565).
- "Minimal movement" is implemented as frequency: `ExpressionProfile` gained
  `gazeHoldMinUs`/`gazeHoldMaxUs` — how long the eye holds a gaze position
  before picking a new one (normal 0.4-3 s, dozing 1.5-4 s, suspicious 2-5 s,
  angry 2.5-6 s). Travel distance is never restricted.

Tests: `tests/eye_render_test.cpp` gained `testGazeReachesArtworkBounds`,
`testAutonomousGazeUsesFullRange`, `testGazeHoldIntervalPerMode` and
`testNoHeadMotionInCrop` (the crop origin must be a pure function of the
resolved gaze); the smoke test now asserts both gaze extremes reach the
coordinate and artwork bounds on both axes.

## 2026-10-11 — F1 fix: per-mode pupil size now derives from the profile

The resting pupil was dead config in three of four modes: `irisScaleBase` was
consumed only by the dozing branch of `restingIrisScale()`, so normal/
suspicious/angry all rendered the constructor's 420 forever and the stare
modes' constriction never appeared.

- `eye.cpp` `rebuildProfile()` now anchors `irisScale_` to the profile base on
  every rebuild (guarded so an explicit `--iris-scale` override survives).
- `ExpressionProfile`: removed the inert `irisScaleRange` overrides from
  suspicious/angry (the span is the dozing dilation driver); kept base 400/330,
  which are correct for their intent.
- Direction verified empirically against the renderer: a LOWER irisScale is a
  MORE constricted pupil (120 -> 4 px pupil, 420 -> 16 px, 720 -> 27 px), so the
  stare modes' lower bases constrict. Two comments stating the opposite were
  corrected (`RenderInput::irisScale`, the dozing dilation test).
- Dozing keeps its inherited `420 - 180 * drowsiness` formula, which NARROWS
  the pupil as drowsiness builds. The project docs have called this
  "dilating"; the direction question is reported to the user, not changed.
- Tests: `testPerModePupilDerivesFromProfile` (normal 420 / suspicious 400 /
  angry 330 / dozing 420->240) and `testPupilAnchorTracksModeAndKeepsOverride`
  (re-anchor on mode change; override survives rebuilds).

## 2026-10-11 — Dozing rework: physiology instead of a sine creep

Dozing previously moved the eye with `512 + wander*80 + sin(now*0.3)*10*d` on
X and a sub-pixel sine on Y -- to a viewer the eye never moved, and the drift
was fixed-period. Reworked to the fatigue physiology, all keyed to drowsiness
d (0 alert .. 1 fighting sleep), replacing the old dozing branch entirely:

- Pupil DILATES with drowsiness (`base + range*d` -> 600 at d=1; measured pupil
  radius 16 px at 420, 22 px at 600). The old formula narrowed it, which is
  backwards.
- Real saccades: dozing now uses the shared target/hold state machine. Hold
  x(1+1.5d), move duration x(1+2d). Measured completed gaze shifts in 60 s:
  20 at d=0, 12 at d=0.5, 9 at d=1. Gaze range 62..896 X / 43..1023 Y.
- Slow + inaccurate: autonomous landings get a jitter of +/-(10+40d) gaze
  units (explicit setGaze still lands exactly).
- Fixation drift + microsaccades: per-eye mean-reverting walk (ceiling
  15+90d units) plus irregular corrective impulses (6+16d units, 40-90 ms
  decay, 120-550 ms intervals). These are the vergence read: the two eyes run
  independent walks, so they intermittently aim at different places, while the
  shared saccade trajectory keeps the display mirror intact.
- Blinks now close, HOLD shut, then open (opening deliberately slower than
  closing). Hold is rolled with probability 0.12+0.5d for 0.06..(0.06+0.5d)s.
  Holding shut >= 0.35 s escalates the blink into a micro-sleep -- the nod-off
  -- complete with the existing recovery and lateral attention-check glance.
  Measured (120 s): 0 nod-offs at d=0.05 and d=0.5, 8 at d=0.95; longest closed
  hold 0.12 s / 0.27 s / 2.07 s.
- Lid async (asymmetry and lead) now scale with drowsiness x(0.4+0.6d), and
  micro-sleep duration x(0.8+0.8d).

Bugs found and fixed while implementing, both mine: the vergence overlay was
added to the crop in gaze units without the crop-unit scale (~14x too large),
and the rewritten triggerEvent never reset eventStartTimeUs, which made every
blink event instantly complete (visible only as missing blinks).

Tests: `testDozingGazeMovesAndSlows`, `testVergenceAppearanceIsBounded`,
`testDozingBlinkHoldsAndEscalatesToNodOff`; dozing pupil assertions flipped to
dilation; `testNoHeadMotionInCrop` now demands an exact gaze->crop mapping
outside dozing and a bounded divergence inside it.

## 2026-10-11 (2) — Dozing feedback pass: transit-only microsaccades, nod-off return + glance

User feedback: the microsaccade jitter read as noise across the whole gaze
behavior, and the nod-off recovery resumed from wherever the wander had drifted
instead of from where the eye had been.

- Microsaccade impulses now fire ONLY while the gaze is transiting between
  points (`gaze_.inMotion`); fixations are carried by the slow drift alone, so
  they read as smooth. The interval window also tightened (80-340 ms).
- The saccade machine is parked for the duration of any nod-off sequence, so it
  cannot pick targets behind the choreography's back.
- New nod-off gaze choreography (`applyDozingNodOffGaze`): while the lids are
  shut the shared gaze rolls slowly off its fixation (~120 units/s, up to
  ~200/140 units); on waking it snaps back to the remembered fixation over
  ~250 ms; a rolled attention check (35%) then glazes laterally
  (+/-240 units, out and back over the 0.55 s check window) before ordinary
  wandering resumes with a fresh hold clock. The return is exact -- verified
  4/4 sequences return to the pre-nod gaze value, 2/4 with a lateral excursion.
- `DrowsinessModel` now actually chains SleepRecovery -> AttentionCheck (the
  AttentionCheck event existed but was never triggered) and no longer pokes the
  gazeWander the animator stopped reading.
- Test: `testDozingNodOffReturnsAndGlances` (exact return, roll-away, glance).

Known limits carried forward: the canthi are still fixed by the static lid maps
(next item -- medial and lateral canthus lowering for the relaxed doze look,
after user verification).

## 2026-10-11 (3) — Shake reduction: stillness first, texture on top

User feedback: too much micro-movement, reading as a shake. Root cause: the
fixation drift was a continuously-running walk, so the eye moved (or tried to)
on literally every frame, and the microsaccade impulses were large enough to
read as twitches.

- The always-on walk is gone. Fixation instability is now intermittent:
  hold still (1.2-3.7 s, shorter when tired) -> drift episode (1.0-3.2 s, a
  small mean-reverting walk inside a ramp-up / hold / ramp-down envelope) ->
  re-fixate. Amplitude cut to 8+20*d gaze units (~2 px max).
- New persistent per-eye convergence OFFSET: a very slow walk (5-8 s time
  constant, +/-(15+45d) units ~ up to 3 px) that gives the pair its vergence
  read -- the eyes sit slightly differently aimed and that misalignment evolves
  over seconds. This is the vergence mechanism now; it never moves fast enough
  to read as motion.
- Microsaccade impulses are transit-only AND small: +/-(2+6d) units
  (~0.15-0.55 px), 70 ms decay, 140-400 ms intervals.
- The nod-off roll now has a size floor (60/40 units) so the wake-up snap-back
  always has something to snap back from, even after a short nod-off.
- Measured motion duty cycle (fraction of frames the rendered position
  changes): 6.5% at d=0.3, 8.6% at d=1.0 -- was ~100%.
