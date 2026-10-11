#include "PupilResponseModel.h"

#include <algorithm>
#include <cmath>

namespace uncanny {
namespace {

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

}  // namespace

PupilResponseModel::PupilResponseModel(PupilResponseConfig config) : config_(config) {
    state_.current = clampUnit(config_.restingPupil);
    state_.target = state_.current;
    state_.rawTarget = state_.current;
}

float PupilResponseModel::responseProgress(std::uint64_t elapsedUs,
                                           float responseTimeSeconds) {
    if (elapsedUs == 0 || responseTimeSeconds <= 0.0F) {
        return 0.0F;
    }
    const float elapsedSeconds = static_cast<float>(
        static_cast<long double>(elapsedUs) / 1'000'000.0L);
    const float tau = std::max(responseTimeSeconds / 4.0F, 0.001F);
    return 1.0F - std::exp(-elapsedSeconds / tau);
}

void PupilResponseModel::update(float target, bool active, std::uint64_t nowUs) {
    const float clampedTarget = clampUnit(target);

    if (!state_.initialized) {
        // Adopting the first reading must happen BEFORE the deadband and
        // hysteresis filtering below. On the very first sample the raw delta is
        // zero by definition, so the deadband would classify it as "no change"
        // and leave the target at the resting pupil -- silently ignoring the
        // sensor until the light happened to move by more than the deadband.
        state_.target = active ? clampedTarget : config_.restingPupil;
        state_.rawTarget = clampedTarget;
        hasLastRaw_ = true;
        lastRawTarget_ = clampedTarget;
        // Jump straight to the target rather than easing in from rest, so the
        // eyes do not visibly dilate on startup.
        state_.current = state_.target;
        state_.initialized = true;
        state_.moving = false;
        state_.direction = 0;
        lastUpdateUs_ = nowUs;
        return;
    }

    if (!active) {
        // Relax to the resting pupil so a disconnected or absent sensor cannot
        // strand the eyes at an extreme dilation.
        state_.moving = true;
        state_.direction = state_.current < config_.restingPupil ? -1 : 1;
        state_.rawTarget = config_.restingPupil;
        state_.target = config_.restingPupil;
    } else {
        state_.rawTarget = clampedTarget;

        if (!hasLastRaw_) {
            hasLastRaw_ = true;
            lastRawTarget_ = clampedTarget;
        }

        const float rawDelta = clampedTarget - lastRawTarget_;

        if (std::fabs(rawDelta) < config_.deadband) {
            // Inside the deadband the tracked target is deliberately left
            // alone. This is what stops sensor quantisation from producing
            // continuous small pupil motion.
        } else if (state_.moving && state_.direction != 0 &&
                   (rawDelta > 0.0F ? 1 : -1) != state_.direction &&
                   std::fabs(rawDelta) < config_.hysteresis) {
            // Reversal not yet credible: hold the current target.
        } else {
            state_.target = clampedTarget;
            state_.direction = clampedTarget < state_.current ? -1 : 1;
            state_.moving = true;
        }
        lastRawTarget_ = clampedTarget;
    }

    const std::uint64_t elapsedUs =
        nowUs >= lastUpdateUs_ ? nowUs - lastUpdateUs_ : 0;
    lastUpdateUs_ = nowUs;

    // Direction is re-derived against the tracked target, not carried over
    // from the last frame, so a settled model reports 0 and picks the correct
    // time constant on the next move.
    const bool constricting = state_.target < state_.current;
    const float responseTime = constricting ? config_.constrictionTime
                                            : config_.dilationTime;
    const float progress = responseProgress(elapsedUs, responseTime);
    state_.current += (state_.target - state_.current) * progress;
    state_.current = clampUnit(state_.current);

    // Light adaptation: a slow pull of the pupil toward the sustained level,
    // independent of the fast constriction/dilation path.
    if (config_.adaptationRate > 0.0F) {
        const float adaptationTarget = state_.rawTarget;
        state_.adaptation += (adaptationTarget - state_.adaptation) *
                             std::min(1.0F, elapsedUs / 1'000'000.0F *
                                                config_.adaptationRate);
        state_.adaptation = clampUnit(state_.adaptation);
    }

    const float remaining = std::fabs(state_.target - state_.current);
    if (remaining < 0.001F) {
        state_.current = state_.target;
        state_.moving = false;
        state_.direction = 0;
    } else {
        state_.direction = state_.target < state_.current ? -1 : 1;
    }
}

void PupilResponseModel::reset(float value) {
    state_.current = clampUnit(value);
    state_.target = state_.current;
    state_.rawTarget = state_.current;
    state_.adaptation = 0.0F;
    state_.initialized = false;
    state_.moving = false;
    state_.direction = 0;
    lastUpdateUs_ = 0;
    lastRawTarget_ = state_.current;
    hasLastRaw_ = true;
}

}  // namespace uncanny
