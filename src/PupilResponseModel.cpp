#include "PupilResponseModel.h"

#include <algorithm>
#include <cmath>

namespace uncanny {

PupilResponseModel::PupilResponseModel(LDRSerialConfig config) : config_(std::move(config)) {}

void PupilResponseModel::update(const LDRSerialSample& sample, std::uint64_t nowUs) {
    if (!active_ || !sample.connected || sample.lastValidUpdateUs == 0) {
        return;
    }

    const float nextTarget = std::max(0.0F, std::min(1.0F, sample.targetPupil));
    if (!state_.initialized) {
        state_.current = nextTarget;
        state_.target = nextTarget;
        state_.initialized = true;
        lastUpdateUs_ = nowUs;
        return;
    }

    const std::uint64_t elapsedUs = nowUs >= lastUpdateUs_ ? nowUs - lastUpdateUs_ : 0;
    const bool constriction = nextTarget < state_.target;
    const float responseTime = constriction ? config_.constrictionTime : config_.dilationTime;
    const float progress = LDRSerialInput::responseProgress(elapsedUs, responseTime);
    state_.target = nextTarget;
    state_.current += (nextTarget - state_.current) * progress;
    state_.current = std::max(0.0F, std::min(1.0F, state_.current));
    lastUpdateUs_ = nowUs;
}

void PupilResponseModel::reset(float value) {
    state_.current = std::max(0.0F, std::min(1.0F, value));
    state_.target = state_.current;
    state_.initialized = true;
    lastUpdateUs_ = 0;
}

}  // namespace uncanny
