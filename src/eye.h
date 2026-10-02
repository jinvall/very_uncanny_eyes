#pragma once

#include "DrowsinessModel.h"
#include "LDRSerialInput.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uncanny {

constexpr std::size_t kEyeSize = 128;
constexpr std::size_t kEyeCount = 2;

struct EyeArtwork {
    std::string_view name;
    const std::uint16_t* sclera;
    int scleraWidth;
    int scleraHeight;
    const std::uint16_t* iris;
    int irisMapWidth;
    int irisMapHeight;
    const std::uint8_t* upperAsymmetric;
    const std::uint8_t* lowerAsymmetric;
    const std::uint8_t* upperSymmetric;
    const std::uint8_t* lowerSymmetric;
    const std::uint16_t* polar;
    int irisWidth;
    int irisHeight;
    int irisMin;
    int irisMax;
    // Optional non-round pupil. Left off (value-initialised) by every variant
    // except the ones that want it, so aggregate init on the shorter lists
    // keeps the previous round-pupil behaviour.
    //
    // 0 = round (default), 1 = vertical slit, 2 = horizontal slit.
    // slitRatio is the short semi-axis as a fraction of the long one.
    //
    // This cannot be expressed through the polar table: distance there is a
    // function of angle alone when a pupilMap is used, which collapses the iris
    // strip to one row per angle (radial spokes). See the pupilMap branch in
    // tablegen.py and the upstream dragon variant for the resulting crescent.
    int pupilShape = 0;
    double slitRatio = 0.0;
};

struct EyeRender {
    std::array<std::uint8_t, kEyeSize * kEyeSize * 4> rgba{};
};

struct EyeState {
    bool inMotion = false;
    int oldX = 512;
    int oldY = 512;
    int newX = 512;
    int newY = 512;
    std::uint64_t moveStartTimeUs = 0;
    std::int64_t moveDurationUs = 0;
    int blinkState = 0;
    std::uint64_t blinkStartTimeUs = 0;
    std::uint64_t blinkDurationUs = 0;
    int upperThreshold = 128;
    int lowerThreshold = 0;
    int dozeClosure = 0;  // 0..255, shared across both eyes in dozing mode
};

struct BlinkState {
    int state = 0;
    std::uint64_t startTimeUs = 0;
    std::uint64_t durationUs = 0;
};

struct EyeFrame {
    EyeRender render;
    int gazeX = 0;
    int gazeY = 0;
    int renderX = 0;
    int renderY = 0;
    int irisScale = 0;
    float environmentalPupil = 0.0F;
    float currentPupil = 0.0F;
    int blinkState = 0;
    int upperThreshold = 0;
    int lowerThreshold = 0;
    int dozeClosure = 0;  // 0..255, shared across both eyes in dozing mode
};

struct RenderSettings {
    bool tracking = true;
    bool symmetricalEyelid = false;
    int irisMin = 120;
    int irisMax = 720;
};

struct AnimationSettings {
    bool autonomousGaze = true;
    bool autonomousBlink = true;
    bool autonomousIris = true;
    std::uint64_t gazeSeed = 1;
    float environmentalIrisWeight = 0.0F;
    float constrictionTime = 0.35F;
    float dilationTime = 0.80F;
};

enum class AnimationMode {
    Normal,
    Dozing,
};

struct DozeSettings {
    std::uint64_t breathPeriodUs = 12'000'000;
    std::uint64_t closeDurationUs = 320'000;
    std::uint64_t openDurationUs = 640'000;
    std::uint64_t blinkIntervalUs = 18'000'000;
    int dozeUpperThreshold = 218;
    int dozeLowerThreshold = 36;
};

class EyeRenderer {
public:
    explicit EyeRenderer(const EyeArtwork& artwork);

    EyeRender render(int gazeX, int gazeY, int irisScale, const EyeState& state,
                     const RenderSettings& settings) const;

private:
    const EyeArtwork& artwork_;
};

class EyeAnimator {
public:
    explicit EyeAnimator(AnimationSettings settings);

    EyeFrame update(std::size_t eyeIndex, std::uint64_t nowUs, int irisScale,
                    const EyeArtwork& artwork, const EyeRenderer& renderer,
                    const RenderSettings& settings);
    void setMode(AnimationMode mode, std::uint64_t nowUs);
    void blink(std::uint64_t nowUs);
    void setGaze(std::size_t eyeIndex, int x, int y);
    void setIrisScale(int scale);
    void setEnvironmentalPupil(float pupil, std::uint64_t nowUs);

    int irisScale() const { return irisScale_; }
    float currentPupil() const { return currentPupil_; }
    float targetPupil() const { return targetPupil_; }
    AnimationMode mode() const { return mode_; }

    // Drowsiness accessors.
    const DrowsinessModel& drowsiness() const { return drowsiness_; }
    DrowsinessModel& drowsiness() { return drowsiness_; }
    void setDrowsinessConfig(const DrowsinessConfig& config) { drowsiness_.setConfig(config); }

private:
    AnimationSettings settings_;
    AnimationMode mode_ = AnimationMode::Normal;
    DozeSettings dozeSettings_;
    EyeState gazeState_;
    BlinkState blinkState_;
    std::array<EyeState, kEyeCount> states_;
    int trackingUpper_ = 128;
    int trackingLower_ = 0;
    std::uint64_t lastTrackingUpdateUs_ = 0;
    bool trackingCurrentFrame_ = false;
    std::uint64_t lastBlinkUs_ = 0;
    std::uint64_t nextBlinkUs_ = 0;
    std::uint64_t dozeBlinkDeadlineUs_ = 0;
    std::uint64_t dozeModeStartUs_ = 0;
    std::uint64_t lastFrameUs_ = 0;
    int irisScale_ = 420;
    int oldIris_ = 420;
    int newIris_ = 420;
    std::uint64_t irisStartUs_ = 0;
    std::uint64_t irisDurationUs_ = 10'000'000;
    int irisRange_ = 300;
    int dozeIrisScale_ = 240;
    float environmentalIrisWeight_ = 0.0F;
    float currentPupil_ = 0.0F;
    float targetPupil_ = 0.0F;
    bool environmentalInitialized_ = false;
    std::uint32_t randomState_;

    DrowsinessModel drowsiness_;

    std::uint32_t random();
    std::uint32_t random(std::uint32_t low, std::uint32_t high);
    void beginIrisMotion(std::uint64_t nowUs);
    void updateDoze(std::uint64_t nowUs);
    int dozeBlinkPosition(std::uint64_t nowUs) const;
};

}  // namespace uncanny
