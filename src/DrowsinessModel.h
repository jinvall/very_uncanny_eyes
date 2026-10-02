#pragma once
#include <cstdint>
#include <array>
namespace uncanny {
// Continuous drowsiness behavioral model.
//
// Replaces the simple sine-wave dozing mode with a physiological state
// machine that produces: gradual eyelid droop, asymmetric eyelid movement,
// variable blink timing/closure speed, intermittent micro-sleeps, head
// instability, and post-recovery lateral checking gaze.
//
// The model is frame-rate independent and runs entirely from elapsed time.
// All events generate their own parameters at trigger time — nothing is
// a fixed, repeatable animation loop.
struct DrowsinessConfig {
    bool enabled = true;
    // Starting drowsiness level (0.0 = alert, 1.0 = fighting sleep).
    float baseline = 0.0F;
    // Accumulation rate: how fast drowsiness grows while no event is active.
    float accumulationRate = 0.015F;   // per second
    // Recovery rate: how fast drowsiness drops after an event resolves.
    float recoveryRate = 0.08F;       // per second
    // ── Eyelid droop ──────────────────────────────────────────────
    // Maximum resting eyelid droop at drowsiness = 1.0.
    // 0.0 = lids fully open, 1.0 = lids fully closed at rest.
    float eyelidDroopMax = 0.35F;
    // ── Normal blink parameters ───────────────────────────────────
    float blinkDurationMin = 0.07F;    // seconds
    float blinkDurationMax = 0.14F;    // seconds
    float blinkIntervalMin = 3.0F;     // seconds between blinks
    float blinkIntervalMax = 8.0F;     // seconds
    // ── Sleepy blink parameters (scaled by drowsiness) ─────────────
    float sleepyBlinkDurationMin = 0.12F;
    float sleepyBlinkDurationMax = 0.35F;
    float sleepyBlinkIntervalMin = 1.0F;
    float sleepyBlinkIntervalMax = 4.0F;
    // ── Partial droop events ──────────────────────────────────────
    // Probability of a partial droop (incomplete closure) per blink.
    float partialDroopProbability = 0.15F;
    // ── Head motion ───────────────────────────────────────────────
    // Base irregular head micro-motion amplitude (coordinate units).
    float headBobAmount = 0.06F;
    // Additional head drop amplitude during micro-sleep events.
    float headDropAmount = 0.22F;
    // Base instability — increases with drowsiness.
    float headInstability = 0.04F;
    // ── Micro-sleep ───────────────────────────────────────────────
    // Per-second probability at drowsiness = 0.
    float microSleepProbBase = 0.0008F;
    // Per-second probability at drowsiness = 1.
    float microSleepProbMax = 0.04F;
    float microSleepDurationMin = 0.25F;  // seconds
    float microSleepDurationMax = 0.75F;  // seconds
    // ── Recovery ──────────────────────────────────────────────────
    float recoveryDurationMin = 0.4F;
    float recoveryDurationMax = 1.2F;
    // ── Attention check ───────────────────────────────────────────
    float attentionCheckProbability = 0.35F;
    float attentionCheckDuration = 0.55F;
    // ── Asymmetry ─────────────────────────────────────────────────
    // Maximum difference between left/right eyelid closure (0..1).
    float lidAsymmetryMax = 0.12F;
    // Maximum time offset between left/right lid start (seconds).
    float lidLeadMax = 0.05F;
};
enum class DrowsinessEvent {
    None,
    NormalBlink,
    LongBlink,
    PartialDroop,
    MicroSleep,
    SleepRecovery,
    AttentionCheck,
};
struct DrowsinessState {
    // Continuous drowsiness value (0.0 = alert, 1.0 = fighting sleep).
    float drowsiness = 0.0F;
    // Currently active behavioral event.
    DrowsinessEvent currentEvent = DrowsinessEvent::None;
    // ── Event timing (microseconds) ───────────────────────────────
    std::uint64_t eventStartTimeUs = 0;
    std::uint64_t eventDurationUs = 0;
    std::uint64_t nextEventTimeUs = 0;
    // ── Blink state ───────────────────────────────────────────────
    bool blinkActive = false;
    float blinkClosure = 0.0F;          // 0.0 = open, 1.0 = fully closed
    float blinkLeftClosure = 0.0F;
    float blinkRightClosure = 0.0F;
    // ── Micro-sleep state ─────────────────────────────────────────
    bool microSleepActive = false;
    float dropStrength = 0.0F;          // 0..1 head drop depth
    float sleepProgress = 0.0F;         // 0..1 event progress
    // ── Recovery state ────────────────────────────────────────────
    bool recoveryActive = false;
    float recoveryProgress = 0.0F;      // 0..1
    // ── Attention check state ─────────────────────────────────────
    bool attentionCheckActive = false;
    float attentionDirection = 0.0F;    // -1 = left, +1 = right
    float attentionProgress = 0.0F;      // 0..1
    // ── Head state (coordinate units, relative to center) ─────────
    float headPitch = 0.0F;             // vertical tilt
    float headYaw = 0.0F;               // horizontal rotation
    float headRoll = 0.0F;              // roll
    // ── Gaze wander (coordinate units) ────────────────────────────
    float gazeWander = 0.0F;
    // ── Per-event generated parameters ────────────────────────────
    float leftLidOffset = 0.0F;         // 0..1, how much left lid droops extra
    float rightLidOffset = 0.0F;        // 0..1, how much right lid droops extra
    float leftLidSpeed = 1.0F;          // multiplier on closure speed
    float rightLidSpeed = 1.0F;         // multiplier on closure speed
    std::int64_t closureDelayUs = 0;    // delay between left and right lid start
};
class DrowsinessModel {
public:
    explicit DrowsinessModel(DrowsinessConfig config = {});
    void update(std::uint64_t nowUs);
    void reset();
    void setConfig(const DrowsinessConfig& config) { config_ = config; }
    void setDrowsiness(float value);
    const DrowsinessState& state() const { return state_; }
    const DrowsinessConfig& config() const { return config_; }
    float drowsiness() const { return state_.drowsiness; }
    DrowsinessEvent currentEvent() const { return state_.currentEvent; }
    bool isEventActive() const {
        return state_.currentEvent != DrowsinessEvent::None;
    }
    // ── Per-frame outputs for the renderer ────────────────────────
    float eyelidDroop() const;
    float leftEyelidPosition() const;   // 0.0 = open, 1.0 = closed
    float rightEyelidPosition() const;
    float headPitch() const { return state_.headPitch; }
    float headYaw() const { return state_.headYaw; }
    float headRoll() const { return state_.headRoll; }
    float gazeWander() const { return state_.gazeWander; }
private:
    void updateDrowsiness(std::uint64_t nowUs, std::uint64_t elapsedUs);
    void scheduleNextEvent(std::uint64_t nowUs);
    void triggerEvent(std::uint64_t nowUs);
    void updateEvent(std::uint64_t nowUs);
    void endEvent(std::uint64_t nowUs);
    float randomFloat();
    float randomFloat(float min, float max);
    float clampUnit(float value) const;
    DrowsinessConfig config_;
    DrowsinessState state_;
    std::uint32_t randomState_;
    // Frame-rate independence.
    std::uint64_t lastUpdateUs_ = 0;
    // Irregular head motion — uses a small set of sine waves with
    // different periods to avoid a clean, recognizable bob.
    static constexpr float kHeadWaveFreqs[5] = {
        0.73F, 1.31F, 2.17F, 3.49F, 5.03F
    };
    static constexpr float kHeadWaveAmplitudes[5] = {
        0.30F, 0.22F, 0.15F, 0.10F, 0.07F
    };
    float headPhase_ = 0.0F;
};
}  // namespace uncanny
