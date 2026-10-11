#include "DrowsinessModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uncanny {

namespace {

}  // namespace

float DrowsinessModel::clampUnit(float value) const {
    return std::max(0.0F, std::min(1.0F, value));
}

DrowsinessModel::DrowsinessModel(DrowsinessConfig config)
    : config_(std::move(config)), randomState_(0x9E3779B9U) {
    state_.drowsiness = clampUnit(config_.baseline);
    scheduleNextEvent(0);
}

void DrowsinessModel::reset() {
    state_ = DrowsinessState{};
    state_.drowsiness = clampUnit(config_.baseline);
    randomState_ = 0x9E3779B9U;
    scheduleNextEvent(0);
}

void DrowsinessModel::setDrowsiness(float value) {
    state_.drowsiness = clampUnit(value);
}

float DrowsinessModel::randomFloat() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_) / static_cast<float>(0xFFFFFFFFU);
}

float DrowsinessModel::randomFloat(float min, float max) {
    return min + (max - min) * randomFloat();
}

void DrowsinessModel::update(std::uint64_t nowUs) {
    if (!config_.enabled) {
        lastUpdateUs_ = nowUs;
        return;
    }

    if (lastUpdateUs_ == 0) {
        lastUpdateUs_ = nowUs;
        return;
    }

    const std::uint64_t elapsedUs = nowUs >= lastUpdateUs_ ? nowUs - lastUpdateUs_ : 0;
    lastUpdateUs_ = nowUs;

    updateDrowsiness(nowUs, elapsedUs);

    // Gaze wander: slow random walk scaled by drowsiness.
    {
        const float dt2 = static_cast<float>(elapsedUs) / 1'000'000.0F;
        state_.gazeWander += (randomFloat() - 0.5F) * 2.0F * dt2 * 15.0F *
                             (0.2F + 0.8F * state_.drowsiness);
        state_.gazeWander = clampUnit(state_.gazeWander) * 2.0F - 1.0F;
    }

    if (state_.currentEvent == DrowsinessEvent::None) {
        if (nowUs >= state_.nextEventTimeUs) {
            triggerEvent(nowUs);
        }
    } else {
        updateEvent(nowUs);
    }
}

void DrowsinessModel::updateDrowsiness(std::uint64_t, std::uint64_t elapsedUs) {
    const float dt = static_cast<float>(elapsedUs) / 1'000'000.0F;

    if (state_.currentEvent == DrowsinessEvent::None) {
        state_.drowsiness = std::min(1.0F,
            state_.drowsiness + config_.accumulationRate * dt);
    } else if (state_.currentEvent == DrowsinessEvent::SleepRecovery) {
        state_.drowsiness = std::max(0.0F,
            state_.drowsiness - config_.recoveryRate * 2.0F * dt);
    } else if (state_.currentEvent == DrowsinessEvent::AttentionCheck) {
        state_.drowsiness = std::max(0.0F,
            state_.drowsiness - config_.recoveryRate * 0.5F * dt);
    }

    state_.drowsiness = clampUnit(state_.drowsiness);
}

void DrowsinessModel::scheduleNextEvent(std::uint64_t nowUs) {
    const float d = state_.drowsiness;
    const float intervalMin = config_.blinkIntervalMin +
        (config_.sleepyBlinkIntervalMin - config_.blinkIntervalMin) * d;
    const float intervalMax = config_.blinkIntervalMax +
        (config_.sleepyBlinkIntervalMax - config_.blinkIntervalMax) * d;
    const float interval = randomFloat(intervalMin, intervalMax);
    state_.nextEventTimeUs = nowUs + static_cast<std::uint64_t>(interval * 1'000'000); // NOLINT
}

void DrowsinessModel::triggerEvent(std::uint64_t nowUs) {
    const float d = state_.drowsiness;
    const float microSleepProb = config_.microSleepProbBase +
        (config_.microSleepProbMax - config_.microSleepProbBase) * d;
    const float roll = randomFloat();

    if (roll < microSleepProb) {
        // Micro-sleep event
        state_.currentEvent = DrowsinessEvent::MicroSleep;
        // A deeper nod-off lasts longer: the window stretches with drowsiness
        // instead of every micro-sleep being the same length.
        const float dur = randomFloat(config_.microSleepDurationMin,
                                      config_.microSleepDurationMax) *
                          (0.8F + 0.8F * d);
        state_.eventStartTimeUs = nowUs;
        state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000);
        state_.microSleepActive = true;
        state_.escalated = true;  // already a nod-off
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d));
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d));
        state_.leftLidSpeed = randomFloat(0.7F, 1.3F);
        state_.rightLidSpeed = randomFloat(0.7F, 1.3F);
        state_.closureDelayUs = static_cast<std::int64_t>(
            randomFloat(0.0F, config_.lidLeadMax * (0.4F + 0.6F * d)) * 1'000'000); // NOLINT

    } else if (roll < microSleepProb + config_.partialDroopProbability * (1.0F - d)) {
        // Partial droop (incomplete blink)
        state_.currentEvent = DrowsinessEvent::PartialDroop;
        state_.eventStartTimeUs = nowUs;
        state_.blinkActive = true;
        state_.blinkClosure = 0.0F;
        state_.blinkCloseFraction = randomFloat(0.4F, 0.55F);
        state_.blinkHoldUs = 0;
        state_.escalated = false;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d) * 0.5F);
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d) * 0.5F);
        state_.leftLidSpeed = randomFloat(0.8F, 1.2F);
        state_.rightLidSpeed = randomFloat(0.8F, 1.2F);
        state_.eventDurationUs = static_cast<std::uint64_t>(
            blinkDurationSeconds(state_.blinkCloseFraction, 0.0F) * 1'000'000);

    } else {
        // Normal or long blink. Both may hold fully closed before opening --
        // and holding shut past the nod-off threshold escalates into a
        // micro-sleep, so a blink is where nod-offs actually come from.
        const bool longBlink = roll < microSleepProb +
            config_.partialDroopProbability * (1.0F - d) + 0.15F * d;
        state_.currentEvent = longBlink ? DrowsinessEvent::LongBlink
                                        : DrowsinessEvent::NormalBlink;
        state_.eventStartTimeUs = nowUs;
        state_.blinkActive = true;
        state_.blinkClosure = 0.0F;
        state_.blinkCloseFraction = randomFloat(0.35F, 0.5F);
        // Whether this blink holds shut at all rises steeply with drowsiness,
        // and when it does, how long it holds rises too: a brief pause at low
        // drowsiness, a long hold (and the nod-off) at high.
        const float holdWanted = randomFloat() < (0.12F + 0.5F * d);
        state_.blinkHoldUs = holdWanted
            ? static_cast<std::uint64_t>(randomFloat(0.06F, 0.06F + 0.5F * d) * 1'000'000)
            : 0;
        state_.escalated = false;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d));
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * (0.4F + 0.6F * d));
        state_.leftLidSpeed = randomFloat(0.7F, 1.3F);
        state_.rightLidSpeed = randomFloat(0.7F, 1.3F);
        state_.closureDelayUs = static_cast<std::int64_t>(
            randomFloat(0.0F, config_.lidLeadMax * (0.4F + 0.6F * d)) * 1'000'000); // NOLINT
        // Close fast, open slower, with the closed hold inserted between them.
        state_.eventDurationUs = static_cast<std::uint64_t>(
            blinkDurationSeconds(state_.blinkCloseFraction,
                                 state_.blinkHoldUs / 1'000'000.0F) * 1'000'000);
    }
}

// Total blink duration in seconds for a given close fraction and closed-hold.
// The closing phase takes `closeFraction` of the moving time; the opening phase
// is deliberately slower than the closing phase, the way real lids move.
float DrowsinessModel::blinkDurationSeconds(float closeFraction,
                                            float holdSeconds) {
    const float d = state_.drowsiness;
    const float moving = std::max(0.2F, closeFraction + (1.0F - closeFraction) * 0.45F);
    const float durationMin = std::max(
        0.05F, (config_.blinkDurationMin +
                (config_.sleepyBlinkDurationMin - config_.blinkDurationMin) * d) / moving);
    const float durationMax = std::max(
        durationMin, (config_.blinkDurationMax +
                      (config_.sleepyBlinkDurationMax - config_.blinkDurationMax) * d) / moving);
    return randomFloat(durationMin, durationMax) + holdSeconds;
}

void DrowsinessModel::updateEvent(std::uint64_t nowUs) {
    const std::uint64_t elapsed = nowUs >= state_.eventStartTimeUs
                                      ? nowUs - state_.eventStartTimeUs
                                      : 0;
    const float progress = state_.eventDurationUs > 0
                               ? static_cast<float>(elapsed) /
                                 static_cast<float>(state_.eventDurationUs)
                               : 1.0F;

    switch (state_.currentEvent) {
        case DrowsinessEvent::NormalBlink:
        case DrowsinessEvent::LongBlink:
        case DrowsinessEvent::PartialDroop: {
            const float closeFraction = std::clamp(state_.blinkCloseFraction, 0.1F, 0.8F);
            const float holdFraction = state_.eventDurationUs > 0
                ? static_cast<float>(state_.blinkHoldUs) /
                  static_cast<float>(state_.eventDurationUs)
                : 0.0F;
            const float openStart = std::min(1.0F, closeFraction + holdFraction);
            if (progress < closeFraction) {
                const float t = closeFraction > 0.0F ? progress / closeFraction : 1.0F;
                const float eased = 3.0F * t * t - 2.0F * t * t * t;
                state_.blinkClosure = eased;
            } else if (progress < openStart) {
                // Closed: the lids hold shut for a moment before opening.
                state_.blinkClosure = 1.0F;
            } else {
                const float span = std::max(1e-4F, 1.0F - openStart);
                const float t = std::clamp((progress - openStart) / span, 0.0F, 1.0F);
                const float eased = 3.0F * t * t - 2.0F * t * t * t;
                state_.blinkClosure = 1.0F - eased;
            }
            state_.blinkLeftClosure = clampUnit(
                state_.blinkClosure * (1.0F + state_.leftLidOffset) * state_.leftLidSpeed);
            state_.blinkRightClosure = clampUnit(
                state_.blinkClosure * (1.0F + state_.rightLidOffset) * state_.rightLidSpeed);

            // Escalation: held shut long enough and this blink has become a
            // nod-off. The event is re-issued as a micro-sleep starting now, so
            // the lids simply stay down and the sleep sequence takes over. The
            // check is bounded to the hold phase itself, so an event that has
            // already finished opening can never escalate.
            if (!state_.escalated && state_.blinkHoldUs > 0) {
                const std::uint64_t closeUs = static_cast<std::uint64_t>(
                    static_cast<float>(state_.eventDurationUs) * closeFraction);
                const std::uint64_t holdEndUs = closeUs + state_.blinkHoldUs;
                const std::uint64_t heldUs = elapsed > closeUs ? elapsed - closeUs : 0;
                if (elapsed <= holdEndUs && heldUs >= static_cast<std::uint64_t>(
                        config_.nodOffHoldSeconds * 1'000'000.0F)) {
                    state_.escalated = true;
                    state_.currentEvent = DrowsinessEvent::MicroSleep;
                    state_.microSleepActive = true;
                    state_.blinkClosure = 1.0F;
                    state_.blinkLeftClosure = 1.0F;
                    state_.blinkRightClosure = 1.0F;
                    const float dur =
                        randomFloat(config_.microSleepDurationMin,
                                    config_.microSleepDurationMax) *
                        (0.8F + 0.8F * state_.drowsiness);
                    state_.eventStartTimeUs = nowUs;
                    state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000);
                    state_.sleepProgress = 0.0F;
                    break;
                }
            }
            if (progress >= 1.0F) {
                endEvent(nowUs);
            }
            break;
        }

        case DrowsinessEvent::MicroSleep: {
            const float sleepEnd = 0.7F;
            if (progress < 0.3F) {
                const float p = progress / 0.3F;
                const float eased = 3.0F * p * p - 2.0F * p * p * p;
                state_.blinkClosure = eased;
            } else if (progress < sleepEnd) {
                state_.blinkClosure = 1.0F;
            } else {
                const float p = (progress - sleepEnd) / (1.0F - sleepEnd);
                const float eased = 3.0F * p * p - 2.0F * p * p * p;
                state_.blinkClosure = 1.0F - eased;
                if (progress >= 1.0F) {
                    state_.currentEvent = DrowsinessEvent::SleepRecovery;
                    state_.recoveryActive = true;
                    state_.recoveryProgress = 0.0F;
                    state_.eventStartTimeUs = nowUs;
                    state_.eventDurationUs = static_cast<std::uint64_t>(
                        randomFloat(config_.recoveryDurationMin,
                                    config_.recoveryDurationMax) * 1'000'000); // NOLINT
                    if (randomFloat() < config_.attentionCheckProbability) {
                        state_.attentionDirection =
                            randomFloat() < 0.5F ? -1.0F : 1.0F;
                    }
                }
            }
            break;
        }

        case DrowsinessEvent::SleepRecovery: {
            state_.recoveryProgress = progress;
            if (progress >= 1.0F) {
                // A nod-off that rolled an attention check glazes sideways
                // before ordinary wandering resumes -- the "did anybody
                // notice" look. The gaze choreography itself lives in the
                // animator, which owns the gaze.
                if (state_.attentionDirection != 0.0F) {
                    state_.currentEvent = DrowsinessEvent::AttentionCheck;
                    state_.attentionCheckActive = true;
                    state_.attentionProgress = 0.0F;
                    state_.eventStartTimeUs = nowUs;
                    state_.eventDurationUs = static_cast<std::uint64_t>(
                        config_.attentionCheckDuration * 1'000'000);
                } else {
                    endEvent(nowUs);
                }
            }
            break;
        }

        case DrowsinessEvent::AttentionCheck: {
            state_.attentionProgress = progress;
            if (progress >= 1.0F) {
                endEvent(nowUs);
            }
            break;
        }

        default:
            break;
    }
}

void DrowsinessModel::endEvent(std::uint64_t nowUs) {
    state_.currentEvent = DrowsinessEvent::None;
    state_.blinkActive = false;
    state_.microSleepActive = false;
    state_.recoveryActive = false;
    state_.attentionCheckActive = false;
    state_.blinkClosure = 0.0F;
    state_.blinkLeftClosure = 0.0F;
    state_.blinkRightClosure = 0.0F;
    state_.blinkHoldUs = 0;
    state_.escalated = false;
    scheduleNextEvent(nowUs);
}

float DrowsinessModel::eyelidDroop() const {
    return config_.eyelidDroopMax * state_.drowsiness;
}

float DrowsinessModel::leftEyelidPosition() const {
    float base = eyelidDroop();
    if (state_.blinkActive || state_.microSleepActive) {
        base = std::max(base, state_.blinkLeftClosure);
    }
    return clampUnit(base);
}

float DrowsinessModel::rightEyelidPosition() const {
    float base = eyelidDroop();
    if (state_.blinkActive || state_.microSleepActive) {
        base = std::max(base, state_.blinkRightClosure);
    }
    return clampUnit(base);
}

}  // namespace uncanny
