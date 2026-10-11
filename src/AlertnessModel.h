#pragma once

#include <cstdint>

namespace uncanny {

// Config for the sustained-stare behaviour shared by the Suspicious and Angry
// modes.
//
// Suspicious and Angry are the mirror image of Dozing: where DrowsinessModel
// opens the eye up and hands it over to a sleep script, this model narrows and
// fixes it. Keeping it separate from the drowsiness model means each mode has
// exactly one owner for the eyelid channel, which is what stops the two
// systems from fighting over the same thresholds.
struct AlertnessConfig {
    bool enabled = true;
    // 0 = gaze is free, 1 = gaze is pinned to the fixation point.
    float fixationStrength = 0.0F;
    // Normalized fixation target, -1..1, where 0 is centre.
    float fixationX = 0.0F;
    float fixationY = 0.0F;
    // Involuntary glances away from the fixation point.
    float dartIntervalMin = 2.0F;  // seconds
    float dartIntervalMax = 5.0F;  // seconds
    float dartHoldTime = 0.35F;   // seconds held at the dart position
    // Dart amplitude, normalized, 0..1 of half the gaze range.
    float dartAmplitude = 0.25F;
    // Extra resting lid closure, 0..1.
    float lidNarrow = 0.0F;
    // Pupil constriction bias, 0..1, where 1 is fully constricted.
    float agitation = 0.0F;
};

class AlertnessModel {
public:
    explicit AlertnessModel(AlertnessConfig config = {});

    void update(std::uint64_t nowUs);
    void reset();
    void setConfig(const AlertnessConfig& config) { config_ = config; }
    const AlertnessConfig& config() const { return config_; }

    // Normalized gaze offset to add on top of the tracked/autonomous gaze, -1..1.
    float gazeOffsetX() const { return gazeOffsetX_; }
    float gazeOffsetY() const { return gazeOffsetY_; }
    // 0..1 lid narrowing contributed by the mode.
    float lidNarrow() const { return config_.lidNarrow; }
    // 0..1 arousal, used to constrict the pupil.
    float agitation() const { return agitation_; }

private:
    void scheduleDart(std::uint64_t nowUs);
    float randomUnit();
    float randomFloat(float min, float max);

    AlertnessConfig config_;
    std::uint32_t randomState_ = 0x2545F491U;

    bool darting_ = false;
    std::uint64_t dartEndUs_ = 0;
    float dartTargetX_ = 0.0F;
    float dartTargetY_ = 0.0F;

    std::uint64_t lastUpdateUs_ = 0;
    std::uint64_t nextDartUs_ = 0;
    float elapsed_ = 0.0F;

    float gazeOffsetX_ = 0.0F;
    float gazeOffsetY_ = 0.0F;
    float agitation_ = 0.0F;
};

}  // namespace uncanny
