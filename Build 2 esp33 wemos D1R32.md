##Build 2 esp33 wemos D1R32##

Uncanny Eyes — LDR Reactive Pupils + Natural Drowsiness Implementation
Uncanny Eyes — LDR Reactive Pupils + Natural Drowsiness
Objective

Implement two related behavioral systems in the desktop version of Adafruit Uncanny Eyes:using  ~/uncanny_eyes

Ambient-light reactive pupils driven by an external ESP32/Arduino LDR over USB serial.
Naturalistic drowsiness/dozing behavior that affects eyelids, pupils, head movement, gaze, timing, and recovery.

These must be implemented as behavioral/physiological systems, not as fixed animations.

The intended result is an eye that feels like it belongs to a living animal rather than an animatronic character performing predetermined animations.

CRITICAL DESIGN PRINCIPLE

Do NOT implement either feature as a sequence of scripted animations.

Avoid:

doze animation
→ play eyelid animation
→ play head-drop animation
→ reset

and avoid:

LDR value
→ directly set pupil size

Instead build a small internal behavioral model in which continuously changing state variables influence the existing eye renderer.

Conceptually:

                  ENVIRONMENT
                      │
                     LDR
                      │
                      ▼
               ambient light
                      │
                      ▼
                pupil target
                      │
                      │
                      ▼
             ┌────────────────┐
             │ PUPIL RESPONSE │
             │     MODEL      │
             └───────┬────────┘
                     │
                     ▼
               actual pupil
                     ▲
                     │
             drowsiness state
                     │
          ┌──────────┴──────────┐
          │                     │
       eyelids               head/gaze
          │                     │
          └──────────┬──────────┘
                     │
                     ▼
              existing renderer
PART 1 — LDR REACTIVE PUPILS
Purpose

An ESP32/Arduino reads an LDR and sends the measurement to the desktop Uncanny Eyes process over USB serial.

Do NOT use Flask.

Do NOT use HTTP.

Do NOT use WebSockets.

Do NOT require a network connection.

Architecture:

LDR
 │
 ▼
ESP32 / Arduino
 │
 │ USB serial
 ▼
Uncanny Eyes
 │
 ▼
filter
 │
 ▼
ambient light estimate
 │
 ▼
pupil target
 │
 ▼
physiological response
 │
 ▼
pupil renderer
Serial Protocol

The preferred protocol is deliberately simple:

723
718
701
685

One ADC value per line.

Also optionally accept:

LDR:723
LDR:718
LDR:701

Malformed lines must simply be ignored.

Do not require JSON.

Reference ESP32 Sender

Provide a test sketch:

const int LDR_PIN = 34;

void setup() {
    Serial.begin(115200);
}

void loop() {
    int value = analogRead(LDR_PIN);
    Serial.println(value);
    delay(50);
}

The desktop implementation must not depend upon exactly 20 Hz.

Accept approximately:

5 Hz       minimum
10–30 Hz   preferred
50+ Hz     acceptable
Serial Reader

Create a dedicated serial-input component.

Conceptual interface:

LDRSerialInput
    connect()
    disconnect()
    reconnect()
    getLatestValue()
    isConnected()
    getPort()
    getLastUpdateTime()

Requirements:

Linux /dev/ttyUSB*
Linux /dev/ttyACM*
configurable baud rate
default 115200
line-based parsing
timestamp valid readings
reject malformed readings
detect disconnect
automatic reconnect
never block the renderer

Serial reading must happen outside the render loop.

The renderer should only consume the latest available sensor state.

LDR Configuration

Adapt to the application's existing configuration system.

Suggested settings:

ldr.enabled = false

ldr.port = "/dev/ttyUSB0"
ldr.baud = 115200

ldr.sensorMin = 0
ldr.sensorMax = 4095

ldr.invert = false

ldr.filterAlpha = 0.15

ldr.minPupil = 0.25
ldr.maxPupil = 1.00

ldr.constrictionTime = 0.35
ldr.dilationTime = 0.80

ldr.disconnectTimeout = 2000
ldr.autoReconnect = true

Do not blindly create a new configuration mechanism if the repository already has one.

LDR Processing Pipeline

Implement:

raw ADC
   ↓
noise filter
   ↓
normalized 0.0–1.0
   ↓
ambient light estimate
   ↓
target pupil size
   ↓
physiological response
   ↓
actual pupil size

Use an exponential filter or equivalent.

Example:

filtered += alpha * (raw - filtered)

Make alpha configurable.

Pupil Mapping

Dark conditions should produce a larger pupil.

Bright conditions should produce a smaller pupil.

Do not assume a linear relationship is visually appropriate.

Start with a nonlinear curve and make it tunable.

Example starting values:

ambient = 0.0 → pupil = 1.00
ambient = 0.5 → pupil = 0.65
ambient = 1.0 → pupil = 0.25

These are initial parameters, not fixed requirements.

Pupil Response

Maintain:

targetPupil
currentPupil

Never simply do:

currentPupil = targetPupil

Instead move toward the target based on elapsed time.

Use separate response times:

constrictionTime
dilationTime

This allows the response to feel less mechanical.

The response must be frame-rate independent.

PART 2 — NATURAL DROWSINESS / DOZING
Purpose

Replace a conventional mechanical "dozing animation" with a continuously evolving state that produces:

gradual eyelid droop
asymmetric eyelid movement
variable blink timing
variable closure speed
variable reopening speed
longer sleepy blinks
intermittent micro-sleeps
head instability
head movement that can lead eyelid closure
sudden recovery from partial sleep
post-recovery lateral checking gaze
gradual accumulation/reduction of sleepiness

The behavior must never look like:

blink animation #4

being repeatedly played.

Drowsiness State

Create a continuous variable:

drowsiness = 0.0 → 1.0

Suggested conceptual ranges:

0.00–0.25   alert
0.25–0.50   relaxed
0.50–0.70   drowsy
0.70–0.85   very drowsy
0.85–1.00   fighting sleep

These are behavioral regions, not hard animation states.

Drowsiness should change gradually.

Do not randomly jump from:

0.20 → 0.90

without a behavioral reason.

Drowsiness Effects

As drowsiness increases:

Eyelids
resting eyelid position gradually lowers
blink frequency becomes less predictable
blink duration increases
closure/reopening speeds become variable
left and right lids do not necessarily move identically
one lid may lead the other slightly
eyelids may partially close without completing a blink
Gaze
attention becomes less stable
gaze wandering increases
occasional fixation becomes softer/less precise
recovery may include lateral checking behavior
Head
subtle bobbing increases
head stability decreases
occasional small downward excursions occur
deeper drowsiness increases probability of a micro-sleep
Eyelid Motion

Each dozing event must generate its own parameters.

Do not use fixed:

close = 500 ms
open = 500 ms

Instead generate ranges.

Conceptually:

closureDuration = random(minClose, maxClose)
openingDuration = random(minOpen, maxOpen)
eventDelay = random(minDelay, maxDelay)

But randomness must be constrained by the current drowsiness state.

Higher drowsiness should bias the system toward:

longer closures
shorter intervals between sleepy events
greater eyelid droop
greater probability of partial/full sleep events
Asynchronous Eyelids

The two eyelids must not always move as a synchronized pair.

For each event allow:

left eyelid start
right eyelid start
left closure speed
right closure speed
left opening speed
right opening speed

with small controlled differences.

Example:

left begins closing
       ↓
right begins 60 ms later
       ↓
left reaches 70% closure
       ↓
right reaches 82%
       ↓
both recover

Do not exaggerate the asymmetry.

The goal is biological irregularity, not a visibly broken eye.

Normal Blink vs Sleepy Blink

Maintain different behavioral distributions.

Normal blink

Generally:

short
quick
low variance
little head involvement
Sleepy blink

Generally:

longer
more variable
greater eyelid droop
possible head movement
possible incomplete recovery

The transition between them should be continuous as drowsiness increases.

Micro-Sleep / Head-Drop Event

The most important dozing behavior is the intermittent loss of consciousness.

The head should NOT simply move downward after the eyelids close.

Instead:

drowsiness rises
        ↓
head begins losing stability
        ↓
head starts dropping
        ↓
eyelids begin closing
        ↓
head continues dropping
        ↓
eyes close
        ↓
brief sleep state
        ↓
consciousness returns
        ↓
head rapidly corrects
        ↓
eyes reopen
        ↓
visual system recovers
        ↓
possible lateral checking glance

The head may therefore lead the eyelid closure.

This is an intentional requirement.

Head Movement

The head must not be static during drowsiness.

Implement subtle continuous micro-motion:

small vertical drift
small rotational drift
small lateral movement

Do not use a clean sine-wave bob.

The motion should be irregular and low amplitude.

As drowsiness increases:

normal:
small irregular movement

drowsy:
more noticeable instability

very drowsy:
occasional larger head drops

micro-sleep:
rapid drop + recovery

Do not make every head movement a complete nod.

Head Lead/Lag

The head and eyelids should have related but independent timing.

Preferred behavior:

head begins dropping
        ↓
eyelids begin closing
        ↓
head reaches lowest point
        ↓
eyes close
        ↓
recovery

Occasionally:

eyelids begin closing
        ↓
head follows

The system should support both.

This prevents the behavior from becoming deterministic.

Micro-Sleep Recovery

Recovery should be sudden enough to look like regained consciousness.

It should not look like:

head animation reverses smoothly

Instead:

loss of control
      ↓
brief sleep
      ↓
rapid correction

But recovery should still be time-based and physically smooth enough to avoid looking like a servo.

Post-Recovery "Did Anyone See That?"

After a sufficiently obvious micro-sleep, optionally trigger a short attention-check behavior.

Conceptually:

wake
 ↓
eyes regain visual control
 ↓
brief focus recovery
 ↓
look left OR right
 ↓
possibly hold briefly
 ↓
return to normal gaze

This behavior must be probabilistic.

Do NOT perform the checking glance after every sleepy event.

Otherwise the checking itself becomes an obvious scripted animation.

Possible outcomes:

micro-sleep
→ no check

micro-sleep
→ left glance

micro-sleep
→ right glance

micro-sleep
→ brief left/right scan

Use weighted randomness.

Pupil Behavior During Drowsiness

Drowsiness must NOT replace the LDR pupil system.

The LDR determines the environmental target.

Drowsiness may influence pupil behavior through the existing/future physiological model.

Architecture:

                 LDR
                  │
                  ▼
          ambient light target
                  │
                  ▼
             pupil model
                  ▲
                  │
             drowsiness
                  │
                  ▼
          behavioral modifier

Do not create two independent systems fighting over the pupil diameter.

Blink + Pupil Interaction

When the eye closes:

do not reset the pupil
do not snap pupil size
continue maintaining the environmental target
allow the physiological pupil model to evolve while the eyelid is closed

When the eye reopens:

pupil should already be moving
toward the appropriate environmental state

Avoid:

eye opens
↓
pupil instantly jumps
Long Closure / Recovery

For a longer sleepy closure, the visual system should require some time to regain its normal state.

Conceptually:

normal blink:
short closure
small pupil adjustment
quick recovery

versus:

long sleepy closure:
long closure
greater pupil change
slower visual recovery
possible brief gaze/focus adjustment

Do not hard-code an exact biological claim such as a specific 1500 ms recovery time.

Instead make recovery proportional to closure duration.

For example:

recoveryTime =
    baseRecovery
    +
    closureDuration * recoveryMultiplier

Clamp the result to sensible minimum/maximum values.

Drowsiness Memory

Drowsiness must have persistence.

Example:

drowsiness rises
↓
micro-sleep
↓
drowsiness decreases slightly
↓
does NOT return to zero

This allows sequences such as:

normal
→ sleepy
→ nod
→ recover
→ still sleepy
→ longer blink
→ recover
→ nod
→ deeper sleep

rather than repeatedly resetting to the same state.

Recovery From Drowsiness

Drowsiness should decrease gradually when the eye remains alert.

Possible future inputs:

user activity
gaze targets
sound
light changes
explicit wake event

For this implementation, provide a simple internal recovery mechanism.

Do not automatically reset drowsiness after every blink.

Behavioral Randomness

All random behavior must be:

bounded
state-dependent
time-based
independently seeded

Avoid repeating identical random sequences.

Do not use randomness as a substitute for a behavioral model.

Bad:

randomly play one of five dozing animations

Good:

continuous drowsiness
+
variable eyelid dynamics
+
variable head stability
+
probabilistic sleep events
Unified Behavioral Architecture

The final architecture should conceptually resemble:

                 ┌─────────────────────┐
                 │ ENVIRONMENTAL INPUT │
                 └──────────┬──────────┘
                            │
                           LDR
                            │
                            ▼
                    ambientLightLevel
                            │
                            ▼
                     targetPupilSize
                            │
                            │
                            ▼
                  ┌─────────────────────┐
                  │  PHYSIOLOGICAL     │
                  │  PUPIL RESPONSE    │
                  └──────────┬──────────┘
                             │
                             ▼
                       actualPupil
                             │
                             ▼
                      eye renderer


                 ┌─────────────────────┐
                 │ DROWSINESS MODEL    │
                 └──────────┬──────────┘
                            │
            ┌───────────────┼────────────────┐
            ▼               ▼                ▼
        eyelids           head             gaze
            │               │                │
            └───────────────┼────────────────┘
                            │
                            ▼
                     behavior state
                            │
                            ▼
                       eye renderer
Shared State Model

Create a central behavioral state rather than scattering variables throughout the renderer.

Conceptually:

EyeBehaviorState

drowsiness
leftLid
rightLid

headPitch
headYaw
headRoll

gazeX
gazeY

ambientLight
targetPupil
currentPupil

sleepEventActive
sleepEventProgress

focusRecovery
attentionCheck

Use the actual project's architecture and naming conventions where possible.

Do not blindly create duplicate state if equivalent structures already exist.

Event System

Implement behavioral events where appropriate.

Possible event types:

NORMAL_BLINK
LONG_BLINK
PARTIAL_DROOP
MICRO_SLEEP
SLEEP_RECOVERY
ATTENTION_CHECK

These are events, not canned animations.

Each event creates parameters that are fed into the normal continuous state update.

Example Micro-Sleep Event Parameters

When a micro-sleep is triggered, generate:

dropStrength
dropDuration
closureDelay
leftLidOffset
rightLidOffset
sleepDuration
recoveryStrength
recoveryDuration
attentionCheckProbability
attentionCheckDirection

These parameters must be generated within ranges influenced by drowsiness.

Timing

Do not use fixed intervals.

Bad:

every 30 seconds:
    doze()

Good:

sleepEventProbability =
    function(drowsiness, elapsedTime)

Use randomized intervals with drowsiness-dependent probability.

At low drowsiness:

micro-sleep probability ≈ very low

At high drowsiness:

micro-sleep probability increases

The exact probabilities should be configurable/testable.

Debug / Developer Diagnostics

Provide a debug mode that can expose:

Drowsiness:       0.73
LDR:              connected
LDR raw:          723
LDR filtered:     718
Ambient:          0.42

Target pupil:     0.68
Current pupil:    0.71

Left lid:         0.32
Right lid:        0.37

Head pitch:       -2.4°
Head yaw:         1.7°

Behavior:
    DROWSY
    MICRO_SLEEP_RECOVERY

This is particularly important while tuning the system.

Do not continuously print these values into the normal log.

Performance Requirements

Neither feature may interfere with rendering performance.

Requirements:

serial input must not block rendering
no blocking sensor reads
no repeated serial connection creation
no per-frame device discovery
no excessive allocations
no high-frequency logging
sensor processing can run at approximately 10–30 Hz
behavioral state can update every render frame
all motion must be frame-rate independent
Failure Behavior
No LDR connected
application starts normally
LDR = unavailable
pupil uses normal behavior
LDR disconnects
renderer continues
last sensor state retained briefly
then environmental control gracefully decays
automatic reconnect attempted
Malformed serial input

Ignore it.

Serial device unavailable at startup

Do not fail application startup.

Configuration disabled

The application should behave as though the feature does not exist.

Configuration

Use the project's existing configuration mechanism.

Suggested conceptual structure:

ldr:
    enabled: false
    port: "/dev/ttyUSB0"
    baud: 115200
    sensorMin: 0
    sensorMax: 4095
    invert: false
    filterAlpha: 0.15
    minPupil: 0.25
    maxPupil: 1.00
    constrictionTime: 0.35
    dilationTime: 0.80
    disconnectTimeout: 2.0
    autoReconnect: true

drowsiness:
    enabled: true

    baseline: 0.0
    accumulationRate: ...
    recoveryRate: ...

    eyelidDroop: ...
    blinkDurationMin: ...
    blinkDurationMax: ...

    headBobAmount: ...
    headDropAmount: ...

    microSleepProbability: ...
    microSleepDurationMin: ...
    microSleepDurationMax: ...

    attentionCheckProbability: ...

Exact names and formats should follow the repository's existing conventions.

Do not create configuration duplication.

Testing
LDR

Serial parser accepts integer lines.

Optional LDR: prefix works.

Invalid lines are ignored.

ADC normalization works.

Inverted sensor works.

Filtering works.

Brightness maps correctly to pupil target.

Pupil response is smooth.

Dilation and constriction use separate response characteristics.

Unplugging device does not crash.

Reconnecting device restores input.

Application runs without LDR.

Drowsiness

Drowsiness changes continuously.

Eyelid resting position changes with drowsiness.

Blink timing becomes more variable.

Blink duration changes with drowsiness.

Left/right eyelids are not perfectly synchronized.

Head develops subtle irregular motion.

Micro-sleeps become possible at higher drowsiness.

Head can lead eyelid closure.

Head-drop recovery works.

Recovery does not look like a reversed animation.

Attention-check behavior is probabilistic.

Attention-check behavior is not triggered every time.

Drowsiness persists after recovery.

Drowsiness can decrease.

No behavior produces a repetitive recognizable loop.

Integration

LDR and drowsiness can operate simultaneously.

They do not overwrite each other's state.

Existing gaze behavior remains functional.

Existing blink behavior remains functional.

Existing renderer remains stable.

Disabling LDR leaves normal behavior intact.

Disabling drowsiness leaves normal behavior intact.

Both systems can be disabled independently.

Manual Test Scenarios
Test 1 — Normal Room

Start the application with normal ambient lighting.

Verify:

pupil settles naturally
no visible jitter
no obvious sensor-to-pupil linkage
Test 2 — Cover LDR

Slowly cover the LDR.

Expected:

ambient light decreases
→ target pupil increases
→ pupil gradually dilates

There must not be an instantaneous jump.

Test 3 — Shine Light

Expose the LDR to a brighter light.

Expected:

ambient light increases
→ target pupil decreases
→ pupil gradually constricts
Test 4 — Sensor Noise

Introduce small fluctuations around a constant light level.

Expected:

pupil remains visually stable
Test 5 — Mild Drowsiness

Increase drowsiness.

Expected:

slight eyelid droop
occasional longer blinks
subtle head bobbing
increasingly variable timing
Test 6 — Deep Drowsiness

Allow drowsiness to become high.

Expected:

head begins to drop
→ lids begin closing
→ brief micro-sleep
→ rapid recovery
Test 7 — Post-Sleep Behavior

Observe several micro-sleeps.

Expected:

some have no attention check
some produce left glance
some produce right glance
timing varies
Test 8 — Combined

While the eye is drowsy, change the ambient light.

Expected:

pupil continues responding to environmental light
eyelids continue behaving independently
head behavior continues
the systems do not fight each other
Important Visual Acceptance Criteria

Reject an implementation if it looks like:

animatronic
servo-driven
fixed animation
looping GIF
robotic blink
mechanical pupil

Specifically reject:

synchronized eyelids every time
identical blink durations
identical blink intervals
identical head drops
head movement that always starts after eyelid movement
pupil movement that immediately follows the LDR
pupil jitter from ADC noise
identical post-sleep glance every time
drowsiness that resets after every event

The desired impression is:

The eye has a continuously changing internal state.

not:

The eye is playing a dozing animation.
Release TODO
Architecture

Inspect repository before modifying code.

Identify renderer.

Identify pupil calculation.

Identify eyelid calculation.

Identify gaze calculation.

Identify head/render transform if available.

Identify configuration system.

Identify existing update/event loop.

Reuse existing architecture wherever possible.

LDR

Serial reader.

Parser.

Filter.

Normalization.

Calibration.

Ambient-light mapping.

Pupil response model.

Configuration.

Diagnostics.

Reconnect handling.

Reference ESP32 sketch.

Drowsiness

Continuous drowsiness state.

Gradual accumulation/recovery.

Eyelid droop.

Variable blink timing.

Variable blink speed.

Independent eyelid offsets.

Head micro-motion.

Head-drop events.

Head/eyelid temporal relationship.

Micro-sleep events.

Recovery behavior.

Focus recovery.

Probabilistic attention checks.

State persistence.

Integration

Unified pupil model.

Unified behavioral state.

No competing pupil controllers.

No competing eyelid controllers.

Preserve existing gaze system.

Preserve existing renderer.

Independent enable/disable controls.

Testing

Unit tests.

Serial tests.

Disconnect/reconnect tests.

Drowsiness state tests.

Timing tests.

Frame-rate independence tests.

Combined LDR + drowsiness tests.

Long-running stability test.

Documentation

Configuration documentation.

ESP32 wiring/reference sketch.

Serial protocol documentation.

Linux serial permissions/troubleshooting.

Calibration instructions.

Drowsiness tuning parameters.

Developer diagnostics.

Feature disable instructions.

Definition of Done

The implementation is complete when an observer can watch the eye for an extended period without being able to identify a repeating animation sequence.

The eye should exhibit:

environmental response
        +
continuous drowsiness
        +
asymmetric eyelids
        +
variable timing
        +
irregular head movement
        +
occasional loss of consciousness
        +
rapid recovery
        +
occasional awareness/checking behavior
        +
physiological pupil response

The LDR should feel like the eye is responding to the room.

The drowsiness system should feel like the eye is fighting to remain conscious.

Neither should feel like an animation being played.

Most importantly, implement both as continuous stateful behavior layered onto the existing Uncanny Eyes renderer, not as isolated animation sequences.
