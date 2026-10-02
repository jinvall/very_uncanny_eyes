#pragma once

#include "LDRSerialInput.h"

#include <cstdint>

namespace uncanny {

struct PupilResponseState {
    float current = 0.0F;
    float target = 0.0F;
    bool initialized = false;
};

class PupilResponseModel {
public:
    explicit PupilResponseModel(LDRSerialConfig config = {});

    void update(const LDRSerialSample& sample, std::uint64_t nowUs);
    void reset(float value = 0.0F);
    float current() const { return state_.current; }
    float target() const { return state_.target; }
    bool active() const { return active_; }
    void setActive(bool active) { active_ = active; }

private:
    LDRSerialConfig config_;
    PupilResponseState state_;
    std::uint64_t lastUpdateUs_ = 0;
    bool active_ = false;
};

}  // namespace uncanny
