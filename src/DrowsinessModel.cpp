#include "DrowsinessModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uncanny {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Head micro-motion frequencies (Hz).
constexpr std::array<float, 5> kHeadWaveFreqs{0.33F, 0.41F, 0.5F, 0.62F, 0.73F};
// Head micro-motion amplitudes (unitless, sum to 1.0).
constexpr std::array<float, 5> kHeadWaveAmplitudes{0.1F, 0.15F, 0.2F, 0.25F, 0.3F};

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
    headPhase_ = 0.0F;
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

    // Irregular head micro-motion: sum of incommensurate sine waves.
    {
        const float dt = static_cast<float>(elapsedUs) / 1'000'000.0F;
        headPhase_ += dt;
        const float d = state_.drowsiness;
        const float instability = config_.headInstability * (0.5F + 0.5F * d);
        float pitch = 0.0F, yaw = 0.0F, roll = 0.0F;
        for (int i = 0; i < 5; ++i) {
            const float phase = headPhase_ * kHeadWaveFreqs[i] * 2.0F * static_cast<float>(kPi);
            const float s = std::sin(phase);
            const float amp = kHeadWaveAmplitudes[i] * config_.headBobAmount;
            pitch += s * amp * (0.8F + 0.2F * static_cast<float>(i));
            yaw  += std::sin(phase * 1.7F + 1.3F) * amp * 0.6F;
            roll += std::sin(phase * 0.6F + 2.1F) * amp * 0.4F;
        }
        state_.headPitch = pitch * (0.3F + 0.7F * d) + instability * d * 3.0F;
        state_.headYaw  = yaw  * (0.3F + 0.7F * d);
        state_.headRoll = roll * (0.3F + 0.7F * d);
    }

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
        const float dur = randomFloat(config_.microSleepDurationMin,
                                      config_.microSleepDurationMax);
        state_.eventStartTimeUs = nowUs;
        state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000);
        state_.microSleepActive = true;
        state_.dropStrength = 0.3F + 0.7F * d;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax);
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax);
        state_.leftLidSpeed = randomFloat(0.7F, 1.3F);
        state_.rightLidSpeed = randomFloat(0.7F, 1.3F);
        state_.closureDelayUs = static_cast<std::int64_t>(
            randomFloat(0.0F, config_.lidLeadMax) * 1'000'000); // NOLINT

    } else if (roll < microSleepProb + config_.partialDroopProbability * (1.0F - d)) {
        // Partial droop (incomplete blink)
        state_.currentEvent = DrowsinessEvent::PartialDroop;
        const float dur = randomFloat(config_.blinkDurationMin * 0.6F,
                                      config_.blinkDurationMax * 0.8F);
        state_.eventStartTimeUs = nowUs;
        state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000); // NOLINT
        state_.blinkActive = true;
        state_.blinkClosure = 0.0F;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * 0.5F);
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * 0.5F);
        state_.leftLidSpeed = randomFloat(0.8F, 1.2F);
        state_.rightLidSpeed = randomFloat(0.8F, 1.2F);

    } else if (roll < microSleepProb + config_.partialDroopProbability * (1.0F - d) + 0.15F * d) {
        // Long (sleepy) blink
        state_.currentEvent = DrowsinessEvent::LongBlink;
        const float durMin = config_.sleepyBlinkDurationMin +
            (config_.blinkDurationMin - config_.sleepyBlinkDurationMin) * (1.0F - d);
        const float durMax = config_.sleepyBlinkDurationMax +
            (config_.blinkDurationMax - config_.sleepyBlinkDurationMax) * (1.0F - d);
        const float dur = randomFloat(durMin, durMax);
        state_.eventStartTimeUs = nowUs;
        state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000); // NOLINT
        state_.blinkActive = true;
        state_.blinkClosure = 0.0F;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax);
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax);
        state_.leftLidSpeed = randomFloat(0.7F, 1.3F);
        state_.rightLidSpeed = randomFloat(0.7F, 1.3F);

    } else {
        // Normal blink
        state_.currentEvent = DrowsinessEvent::NormalBlink;
        const float dur = randomFloat(config_.blinkDurationMin,
                                      config_.blinkDurationMax);
        state_.eventStartTimeUs = nowUs;
        state_.eventDurationUs = static_cast<std::uint64_t>(dur * 1'000'000); // NOLINT
        state_.blinkActive = true;
        state_.blinkClosure = 0.0F;
        state_.leftLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * 0.5F);
        state_.rightLidOffset = randomFloat(0.0F, config_.lidAsymmetryMax * 0.5F);
        state_.leftLidSpeed = randomFloat(0.9F, 1.1F);
        state_.rightLidSpeed = randomFloat(0.9F, 1.1F);
    }
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
            const float eased = 3.0F * progress * progress -
                                2.0F * progress * progress * progress;
            state_.blinkClosure = eased;
            state_.blinkLeftClosure = clampUnit(
                eased * (1.0F + state_.leftLidOffset) * state_.leftLidSpeed);
            state_.blinkRightClosure = clampUnit(
                eased * (1.0F + state_.rightLidOffset) * state_.rightLidSpeed);
            if (state_.currentEvent == DrowsinessEvent::LongBlink) {
                state_.headPitch -= eased * config_.headDropAmount * 0.3F;
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
                state_.headPitch -= eased * config_.headDropAmount * state_.dropStrength;
                state_.blinkClosure = eased;
            } else if (progress < sleepEnd) {
                state_.headPitch -= config_.headDropAmount * state_.dropStrength;
                state_.blinkClosure = 1.0F;
            } else {
                const float p = (progress - sleepEnd) / (1.0F - sleepEnd);
                const float eased = 3.0F * p * p - 2.0F * p * p * p;
                state_.headPitch += eased * config_.headDropAmount * state_.dropStrength;
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
            if (state_.attentionDirection != 0.0F) {
                state_.gazeWander += state_.attentionDirection * progress * 0.5F;
            }
            if (progress >= 1.0F) {
                endEvent(nowUs);
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
    state_.headPitch = 0.0F;
    state_.dropStrength = 0.0F;
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
