#include "AlertnessModel.h"

#include <algorithm>
#include <cmath>

namespace uncanny {
namespace {

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

float clampSigned(float value) {
    return std::max(-1.0F, std::min(1.0F, value));
}

constexpr double kPi = 3.14159265358979323846;

}  // namespace

AlertnessModel::AlertnessModel(AlertnessConfig config) : config_(config) {
    reset();
}

void AlertnessModel::reset() {
    darting_ = false;
    dartEndUs_ = 0;
    dartTargetX_ = 0.0F;
    dartTargetY_ = 0.0F;
    lastUpdateUs_ = 0;
    elapsed_ = 0.0F;
    gazeOffsetX_ = 0.0F;
    gazeOffsetY_ = 0.0F;
    agitation_ = 0.0F;
    nextDartUs_ = 0;
    randomState_ = 0x2545F491U;
}

float AlertnessModel::randomUnit() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<float>(randomState_) / static_cast<float>(0xFFFFFFFFU);
}

float AlertnessModel::randomFloat(float min, float max) {
    if (max <= min) {
        return min;
    }
    return min + (max - min) * randomUnit();
}

void AlertnessModel::scheduleDart(std::uint64_t nowUs) {
    if (config_.dartAmplitude <= 0.0F) {
        darting_ = false;
        nextDartUs_ = nowUs + static_cast<std::uint64_t>(
            std::max(0.1F, config_.dartIntervalMin) * 1'000'000.0F);
        return;
    }
    // A dart is a point offset from the fixation point, biased away from the
    // centre so glances land in the periphery the way real checking behaviour
    // does, rather than always drifting to the same spot.
    const float angle = randomFloat(0.0F, static_cast<float>(2.0 * kPi));
    const float reach = config_.dartAmplitude * randomFloat(0.45F, 1.0F);
    dartTargetX_ = clampSigned(config_.fixationX + std::cos(angle) * reach);
    dartTargetY_ = clampSigned(config_.fixationY + std::sin(angle) * reach * 0.6F);
    darting_ = true;
    dartEndUs_ = nowUs + static_cast<std::uint64_t>(
        std::max(0.05F, config_.dartHoldTime) * 1'000'000.0F);
    nextDartUs_ = dartEndUs_ + static_cast<std::uint64_t>(
        randomFloat(config_.dartIntervalMin, config_.dartIntervalMax) * 1'000'000.0F);
}

void AlertnessModel::update(std::uint64_t nowUs) {
    if (!config_.enabled) {
        gazeOffsetX_ = 0.0F;
        gazeOffsetY_ = 0.0F;
        agitation_ = 0.0F;
        lastUpdateUs_ = nowUs;
        return;
    }

    if (lastUpdateUs_ == 0) {
        lastUpdateUs_ = nowUs;
        // Prime the dart schedule so the first glance does not land on the
        // very first frame.
        nextDartUs_ = nowUs + static_cast<std::uint64_t>(
            randomFloat(config_.dartIntervalMin, config_.dartIntervalMax) * 1'000'000.0F);
        return;
    }

    const std::uint64_t elapsedUs = nowUs >= lastUpdateUs_ ? nowUs - lastUpdateUs_ : 0;
    lastUpdateUs_ = nowUs;
    const float dt = static_cast<float>(
        static_cast<long double>(elapsedUs) / 1'000'000.0L);
    elapsed_ += dt;

    // ── Dart scheduling ───────────────────────────────────────────────────
    if (darting_) {
        if (nowUs >= dartEndUs_) {
            darting_ = false;
        }
    } else if (nowUs >= nextDartUs_) {
        scheduleDart(nowUs);
    }

    // ── Gaze offset ───────────────────────────────────────────────────────
    // Ease toward whichever target is current: the dart position while darting,
    // otherwise back to the fixation point. The fixation pull is applied on top,
    // so a mode with fixationStrength 0 still darts freely.
    const float targetX = darting_ ? dartTargetX_ : config_.fixationX;
    const float targetY = darting_ ? dartTargetY_ : config_.fixationY;
    // Approach rate: fast enough to read as a saccade within a few frames.
    const float follow = 1.0F - std::exp(-dt / 0.09F);
    gazeOffsetX_ += (targetX - gazeOffsetX_) * follow;
    gazeOffsetY_ += (targetY - gazeOffsetY_) * follow;

    // fixationStrength blends the raw dart behaviour back toward the fixation
    // point, which is what turns "glancing around" into "watching you".
    const float hold = clampUnit(config_.fixationStrength);
    gazeOffsetX_ = config_.fixationX +
                   (gazeOffsetX_ - config_.fixationX) * (1.0F - hold * 0.85F);
    gazeOffsetY_ = config_.fixationY +
                   (gazeOffsetY_ - config_.fixationY) * (1.0F - hold * 0.85F);
    gazeOffsetX_ = clampSigned(gazeOffsetX_);
    gazeOffsetY_ = clampSigned(gazeOffsetY_);

    // ── Agitation ─────────────────────────────────────────────────────────
    // A high floor that wobbles, so an angry eye never looks placid even in
    // the flat part of the cycle.
    const float wobble = std::sin(elapsed_ * static_cast<float>(2.0 * kPi) * 0.9F);
    agitation_ = clampUnit(config_.agitation * (0.9F + 0.1F * wobble));
}

}  // namespace uncanny
