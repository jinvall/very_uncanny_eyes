#pragma once

#include <cstdint>

namespace uncanny {

// Physiological pupil response.
//
// This is the single owner of "how fast and how far the pupil moves toward a
// light target". It is deliberately decoupled from serial transport: the LDR
// reader only has to hand over a normalized light level, and the same model
// would serve a future I2C photodiode or an on-screen slider with no changes.
//
// It is a first-order low-pass with asymmetric time constants plus three
// corrections that a bare exponential pass-through lacks and that are what
// make the motion read as physiological rather than twitchy:
//
//   * deadband     - a small target change is ignored entirely, so sensor
//                    quantisation does not keep the pupil twitching.
//   * hysteresis   - after moving one way, the target must clear an extra
//                    threshold before the response reverses, so noise around
//                    the set point cannot cause rapid direction flipping.
//   * adaptation   - a very slow drift of the resting pupil toward the
//                    sustained light level, standing in for the slow
//                    light-adaptation component of real pupillary response.
struct PupilResponseConfig {
    // Time constants. Constriction (bright response) is faster than dilation.
    float constrictionTime = 0.35F;  // seconds
    float dilationTime = 0.80F;      // seconds

    // Target changes smaller than this are ignored entirely.
    float deadband = 0.02F;
    // Extra target distance required to reverse an in-flight direction.
    float hysteresis = 0.03F;
    // Very slow drift toward the sustained light level, in pupil units/second.
    // 0 disables adaptation.
    float adaptationRate = 0.004F;

    // Resting pupil when the model is inactive, so the eyes are never left in
    // a state that only makes sense while a sensor happens to be attached.
    float restingPupil = 0.5F;
};

struct PupilResponseState {
    float current = 0.0F;
    float target = 0.0F;   // deadband-filtered target actually tracking
    float rawTarget = 0.0F;
    float adaptation = 0.0F;  // accumulated light adaptation offset
    bool initialized = false;
    bool moving = false;      // true while constricting or dilating
    int direction = 0;        // -1 constricting, +1 dilating, 0 settled
};

class PupilResponseModel {
public:
    explicit PupilResponseModel(PupilResponseConfig config = {});

    // Feeds one normalized light target in 0..1 (1 = fully dilated / dark).
    // `active` false makes the model relax to its resting pupil.
    void update(float target, bool active, std::uint64_t nowUs);

    void reset(float value = 0.0F);
    void setConfig(const PupilResponseConfig& config) { config_ = config; }
    const PupilResponseConfig& config() const { return config_; }
    const PupilResponseState& state() const { return state_; }

    float current() const { return state_.current; }
    float target() const { return state_.target; }
    bool initialized() const { return state_.initialized; }
    bool moving() const { return state_.moving; }

    // Frame-rate independent exponential progress over `responseTimeSeconds`.
    static float responseProgress(std::uint64_t elapsedUs, float responseTimeSeconds);

private:
    PupilResponseConfig config_;
    PupilResponseState state_;
    std::uint64_t lastUpdateUs_ = 0;
    float lastRawTarget_ = 0.0F;
    bool hasLastRaw_ = false;
};

}  // namespace uncanny
