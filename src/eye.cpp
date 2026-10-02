#include "eye.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace uncanny {

namespace {

constexpr int kScreenSize = static_cast<int>(kEyeSize);
constexpr int kCoordinateMax = 1023;
constexpr int kNoBlink = 0;
constexpr int kClosing = 1;
constexpr int kOpening = 2;

constexpr std::array<std::uint8_t, 256> kEase = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 3,
    3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 10,
    11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 44, 45, 46, 47, 48, 50, 51, 52, 53, 54, 56, 57, 58,
    60, 61, 62, 63, 65, 66, 67, 69, 70, 72, 73, 74, 76, 77, 78, 80,
    81, 83, 84, 85, 87, 88, 90, 91, 93, 94, 96, 97, 98, 100, 101, 103,
    104, 106, 107, 109, 110, 112, 113, 115, 116, 118, 119, 121, 122, 124, 125, 127,
    128, 130, 131, 133, 134, 136, 137, 139, 140, 142, 143, 145, 146, 148, 149, 151,
    152, 154, 155, 157, 158, 159, 161, 162, 164, 165, 167, 168, 170, 171, 172, 174,
    175, 177, 178, 179, 181, 182, 183, 185, 186, 188, 189, 190, 192, 193, 194, 195,
    197, 198, 199, 201, 202, 203, 204, 205, 207, 208, 209, 210, 211, 213, 214, 215,
    216, 217, 218, 219, 220, 221, 222, 224, 225, 226, 227, 228, 228, 229, 230, 231,
    232, 233, 234, 235, 236, 237, 237, 238, 239, 240, 240, 241, 242, 243, 243, 244,
    245, 245, 246, 246, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252,
    252, 253, 253, 253, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255};

std::uint32_t rgb565ToRgba(std::uint16_t value) {
    const std::uint8_t r = static_cast<std::uint8_t>((value >> 11) & 0x1fU);
    const std::uint8_t g = static_cast<std::uint8_t>((value >> 5) & 0x3fU);
    const std::uint8_t b = static_cast<std::uint8_t>(value & 0x1fU);
    const std::uint8_t rr = static_cast<std::uint8_t>((r << 3) | (r >> 2));
    const std::uint8_t gg = static_cast<std::uint8_t>((g << 2) | (g >> 4));
    const std::uint8_t bb = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    return (static_cast<std::uint32_t>(rr) << 24) |
           (static_cast<std::uint32_t>(gg) << 16) |
           (static_cast<std::uint32_t>(bb) << 8) | 0xffU;
}

int mapRange(int value, int fromLow, int fromHigh, int toLow, int toHigh) {
    const std::int64_t numerator =
        static_cast<std::int64_t>(value - fromLow) * (toHigh - toLow);
    const std::int64_t denominator = fromHigh - fromLow;
    return static_cast<int>(toLow + numerator / denominator);
}

}  // namespace

EyeRenderer::EyeRenderer(const EyeArtwork& artwork) : artwork_(artwork) {}

EyeRender EyeRenderer::render(int gazeX, int gazeY, int irisScale,
                               const EyeState& state,
                               const RenderSettings& settings) const {
    EyeRender output;

    const int irisThreshold = (128 * (1023 - irisScale) + 512) / 1024;
    const std::uint32_t irisScaleFixed =
        static_cast<std::uint32_t>(artwork_.irisMapHeight) * 65536U /
        static_cast<std::uint32_t>(std::max(1, irisThreshold));

    const int scleraRangeX = std::max(0, artwork_.scleraWidth - kScreenSize);
    const int scleraRangeY = std::max(0, artwork_.scleraHeight - kScreenSize);
    const int scleraX = std::clamp(gazeX, 0, scleraRangeX);
    const int scleraY = std::clamp(gazeY, 0, scleraRangeY);

    const std::uint8_t* upper =
        settings.symmetricalEyelid ? artwork_.upperSymmetric : artwork_.upperAsymmetric;
    const std::uint8_t* lower =
        settings.symmetricalEyelid ? artwork_.lowerSymmetric : artwork_.lowerAsymmetric;

    const std::uint8_t upperThreshold =
        std::clamp(state.upperThreshold, 0, 255);
    const std::uint8_t lowerThreshold =
        std::clamp(state.lowerThreshold, 0, 255);

    const bool isDefaultEye = (artwork_.name == "default");
    const int dozeClosure = state.dozeClosure;

    for (int y = 0; y < kScreenSize; ++y) {
        for (int x = 0; x < kScreenSize; ++x) {
            std::uint32_t source = 0;
            if ((lower[y * kScreenSize + x] <= lowerThreshold) ||
                (upper[y * kScreenSize + x] <= upperThreshold)) {
                source = 0;
            } else {
                const int irisX = scleraX + x - (artwork_.scleraWidth - artwork_.irisWidth) / 2;
                const int irisY = scleraY + y - (artwork_.scleraHeight - artwork_.irisHeight) / 2;
                if (irisY < 0 || irisY >= artwork_.irisHeight ||
                    irisX < 0 || irisX >= artwork_.irisWidth) {
                    source = artwork_.sclera[(scleraY + y) * artwork_.scleraWidth +
                                             scleraX + x];
                } else {
                    // The iris disc always spans the polar circle out to
                    // irisThreshold. What differs is how the pupil is carved out
                    // of it: by default the radial distance field carves a
                    // round pupil, but that field is radial by construction and
                    // cannot express a slit, so a shaped pupil replaces the
                    // round test outright rather than adding to it.
                    const double centre = artwork_.irisHeight / 2.0;
                    const double dx = irisX + 0.5 - centre;
                    const double dy = irisY + 0.5 - centre;
                    const double rho2 = dx * dx + dy * dy;
                    // The iris can only exist inside the polar circle, which is
                    // what bounds it -- NOT the threshold. The threshold sets how
                    // much of that circle is iris, i.e. where the pupil starts.
                    const bool withinIris =
                        irisThreshold > 0 && rho2 <= centre * centre;

                    bool drawIris = false;
                    if (artwork_.pupilShape != 0) {
                        bool inSlit = false;
                        if (withinIris) {
                            // Pupil extent matches the round case so the slit
                            // occupies the same overall size, and widens as the
                            // eye dilates exactly as a round pupil does.
                            const double pupilRadius =
                                (127.0 - irisThreshold) / 128.0 * centre;
                            if (pupilRadius > 0.5) {
                                const double ax =
                                    (artwork_.pupilShape == 1)
                                        ? pupilRadius * artwork_.slitRatio
                                        : pupilRadius;
                                const double ay = pupilRadius;
                                inSlit = (dx * dx) / (ax * ax) +
                                             (dy * dy) / (ay * ay) <= 1.0;
                            }
                        }
                        drawIris = withinIris && !inSlit;
                    } else {
                        const std::uint16_t polar =
                            artwork_.polar[irisY * artwork_.irisWidth + irisX];
                        drawIris = (polar & 0x7f) < irisThreshold;
                    }

                    if (drawIris) {
                        const std::uint16_t polar =
                            artwork_.polar[irisY * artwork_.irisWidth + irisX];
                        const int distance = polar & 0x7f;
                        int scaledDistance = static_cast<int>(
                            static_cast<std::uint64_t>(distance) * irisScaleFixed / 65536U);
                        // Clamp: at irisScale 1023 the threshold collapses to 0
                        // and irisScaleFixed saturates, which would index past
                        // the strip.
                        scaledDistance = std::clamp(scaledDistance, 0,
                                                    artwork_.irisMapHeight - 1);
                        const int angle = (artwork_.irisMapWidth * (polar >> 7)) / 512;
                        source = artwork_.iris[scaledDistance * artwork_.irisMapWidth + angle];
                    } else {
                        source = artwork_.sclera[(scleraY + y) * artwork_.scleraWidth +
                                                 scleraX + x];
                    }
                }
            }
            const std::uint32_t rgba = rgb565ToRgba(source);
            const std::size_t destination =
                (static_cast<std::size_t>(y) * kEyeSize + x) * 4;

            std::uint32_t finalRgba = rgba;
            if (isDefaultEye && dozeClosure > 0) {
                int caruncleDist = 0;
                if (x < 10) caruncleDist = 10 - x;
                else if (x > kScreenSize - 10) caruncleDist = x - (kScreenSize - 10);
                if (caruncleDist > 0) {
                    const float fade = 1.0f - static_cast<float>(dozeClosure) / 255.0f;
                    const float distFactor = static_cast<float>(caruncleDist) / 10.0f;
                    const float alpha = fade * distFactor;
                    const std::uint8_t r = static_cast<std::uint8_t>(rgba >> 24);
                    const std::uint8_t g = static_cast<std::uint8_t>((rgba >> 16) & 0xff);
                    const std::uint8_t b = static_cast<std::uint8_t>((rgba >> 8) & 0xff);
                    const std::uint8_t rf = static_cast<std::uint8_t>(r * alpha);
                    const std::uint8_t gf = static_cast<std::uint8_t>(g * alpha);
                    const std::uint8_t bf = static_cast<std::uint8_t>(b * alpha);
                    finalRgba = (static_cast<std::uint32_t>(rf) << 24) |
                                (static_cast<std::uint32_t>(gf) << 16) |
                                (static_cast<std::uint32_t>(bf) << 8) | 0xffU;
                }
            }

            output.rgba[destination] = static_cast<std::uint8_t>(finalRgba >> 24);
            output.rgba[destination + 1] = static_cast<std::uint8_t>(finalRgba >> 16);
            output.rgba[destination + 2] = static_cast<std::uint8_t>(finalRgba >> 8);
            output.rgba[destination + 3] = static_cast<std::uint8_t>(finalRgba);
        }
    }

    return output;
}

EyeAnimator::EyeAnimator(AnimationSettings settings)
    : settings_(settings),
      environmentalIrisWeight_(settings.environmentalIrisWeight),
      randomState_(static_cast<std::uint32_t>(settings.gazeSeed)) {
    if (randomState_ == 0) {
        randomState_ = 0x6d2b79f5U;
    }
}

void EyeAnimator::setMode(AnimationMode mode, std::uint64_t nowUs) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    dozeModeStartUs_ = nowUs;
    lastFrameUs_ = ~std::uint64_t(0);
    if (mode == AnimationMode::Dozing) {
        blinkState_.state = kNoBlink;
        blinkState_.startTimeUs = 0;
        blinkState_.durationUs = 0;
        nextBlinkUs_ = 0;  // Reset so first Normal-mode blink after dozing fires promptly
        drowsiness_.reset();
        // Ensure dozing mode starts with visible eyelid droop. If the baseline
        // was left at 0.0 (the default), set a moderate starting drowsiness so
        // the breathing envelope is immediately visible rather than requiring
        // many seconds of accumulation to become noticeable.
        if (drowsiness_.config().baseline == 0.0F) {
            drowsiness_.setDrowsiness(0.3F);
        }
        for (EyeState& state : states_) {
            state.blinkState = kNoBlink;
            state.blinkStartTimeUs = 0;
            state.blinkDurationUs = 0;
        }
    }
}

void EyeAnimator::blink(std::uint64_t nowUs) {
    if (blinkState_.state != kNoBlink) {
        return;  // Already blinking — return early
    }
    blinkState_.state = kClosing;
    blinkState_.startTimeUs = nowUs;
    blinkState_.durationUs = static_cast<std::uint64_t>(random(60'000, 140'001));
    // Schedule the next autonomous blink with a 3-8 second interval,
    // matching the upstream Adafruit implementation
    // (blinkDuration * 3 + random(4'000'000)).
    lastBlinkUs_ = nowUs;
    nextBlinkUs_ = nowUs + static_cast<std::uint64_t>(random(3'000'000, 8'000'001));
}

void EyeAnimator::setGaze(std::size_t eyeIndex, int x, int y) {
    if (eyeIndex >= states_.size()) {
        return;
    }
    EyeState& gaze = gazeState_;
    gaze.oldX = gaze.newX;
    gaze.oldY = gaze.newY;
    gaze.newX = std::clamp(x, 0, kCoordinateMax);
    gaze.newY = std::clamp(y, 0, kCoordinateMax);
    gaze.inMotion = true;
    gaze.moveStartTimeUs = 0;
    gaze.moveDurationUs = 0;
}

void EyeAnimator::setIrisScale(int scale) {
    irisScale_ = std::clamp(scale, 0, 1023);
}

void EyeAnimator::setEnvironmentalPupil(float pupil, std::uint64_t nowUs) {
    targetPupil_ = pupil;
    if (!environmentalInitialized_) {
        currentPupil_ = pupil;
        environmentalInitialized_ = true;
    }
}

std::uint32_t EyeAnimator::random() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return randomState_;
}

std::uint32_t EyeAnimator::random(std::uint32_t low, std::uint32_t high) {
    if (low >= high) {
        return low;
    }
    return low + random() % (high - low + 1);
}

void EyeAnimator::beginIrisMotion(std::uint64_t nowUs) {
    oldIris_ = irisScale_;
    newIris_ = static_cast<int>(random(irisRange_, 1024 - irisRange_));
    irisStartUs_ = nowUs;
    irisDurationUs_ = static_cast<std::uint64_t>(random(2'000'000, 6'000'001));
}

EyeFrame EyeAnimator::update(std::size_t eyeIndex, std::uint64_t nowUs, int irisScale,
                              const EyeArtwork& artwork, const EyeRenderer& renderer,
                              const RenderSettings& settings) {
    if (eyeIndex >= states_.size()) {
        eyeIndex = 0;
    }
    EyeState& state = states_[eyeIndex];

    if (mode_ == AnimationMode::Dozing) {
        // Update drowsiness model once per frame.
        if (lastFrameUs_ != nowUs) {
            drowsiness_.update(nowUs);
            lastFrameUs_ = nowUs;
        }
    } else if (settings_.autonomousBlink || blinkState_.state != kNoBlink) {
        // Only initiate a new blink when no blink is in progress AND the
        // scheduled interval has elapsed. The state machine below (lines
        // 407-434) handles advancing an in-progress blink without needing
        // blink() to be called again.
        if (blinkState_.state == kNoBlink && nowUs >= nextBlinkUs_) {
            blink(nowUs);
        }
    }

    EyeState& gaze = gazeState_;
    int eyeX = gaze.oldX;
    int eyeY = gaze.oldY;

    if (mode_ == AnimationMode::Dozing) {
        // Drowsiness-based gaze: subtle wander plus occasional lateral checks.
        const float wander = drowsiness_.gazeWander();
        const float d = drowsiness_.drowsiness();
        eyeX = static_cast<int>(512.0 + wander * 80.0 +
                                std::sin(static_cast<float>(nowUs) / 1'000'000.0F * 0.3) * 10.0 * d);
        eyeY = static_cast<int>(512.0 +
                                std::sin(static_cast<float>(nowUs) / 1'000'000.0F * 0.5) * 8.0 * d);
    } else if (settings_.autonomousGaze) {
        const std::int64_t elapsed = static_cast<std::int64_t>(nowUs) -
                                     static_cast<std::int64_t>(gaze.moveStartTimeUs);
        if (gaze.inMotion) {
            if (elapsed >= gaze.moveDurationUs) {
                gaze.inMotion = false;
                gaze.moveDurationUs = static_cast<std::int64_t>(random(0, 3'000'001));
                gaze.moveStartTimeUs = nowUs;
                gaze.oldX = gaze.newX;
                gaze.oldY = gaze.newY;
                eyeX = gaze.oldX;
                eyeY = gaze.oldY;
            } else {
                const std::uint64_t denominator = static_cast<std::uint64_t>(gaze.moveDurationUs);
                const std::size_t easeIndex = denominator == 0
                                                  ? 0
                                                  : std::min<std::uint64_t>(
                                                        255, 255 * static_cast<std::uint64_t>(elapsed) / denominator);
                const int interpolation = kEase[easeIndex] + 1;
                eyeX = gaze.oldX + static_cast<int>(
                    static_cast<std::int64_t>(gaze.newX - gaze.oldX) * interpolation / 256);
                eyeY = gaze.oldY + static_cast<int>(
                    static_cast<std::int64_t>(gaze.newY - gaze.oldY) * interpolation / 256);
            }
        } else if (elapsed > gaze.moveDurationUs) {
            int dx = 0;
            int dy = 0;
            do {
                gaze.newX = static_cast<int>(random(0, 1024));
                gaze.newY = static_cast<int>(random(0, 1024));
                dx = gaze.newX * 2 - kCoordinateMax;
                dy = gaze.newY * 2 - kCoordinateMax;
            } while (static_cast<std::int64_t>(dx) * dx +
                     static_cast<std::int64_t>(dy) * dy >
                     static_cast<std::int64_t>(kCoordinateMax) * kCoordinateMax);
            gaze.moveDurationUs = static_cast<std::int64_t>(random(72'000, 144'001));
            gaze.moveStartTimeUs = nowUs;
            gaze.inMotion = true;
        }
    }

    int renderX = mapRange(eyeX, 0, kCoordinateMax, 0, artwork.scleraWidth - kScreenSize);
    if (eyeIndex == 1) {
        renderX = artwork.scleraWidth - kScreenSize - renderX;
    }
    renderX = std::clamp(renderX + 4, 0, artwork.scleraWidth - kScreenSize);
    int renderY = std::clamp(
        mapRange(eyeY, 0, kCoordinateMax, 0, artwork.scleraHeight - kScreenSize),
        0, artwork.scleraHeight - kScreenSize);

    // Apply head pitch/yaw/roll from drowsiness model.
    if (mode_ == AnimationMode::Dozing) {
        renderX += static_cast<int>(drowsiness_.headYaw() * 20.0F);
        renderY += static_cast<int>(drowsiness_.headPitch() * 20.0F);
        renderX = std::clamp(renderX, 0, artwork.scleraWidth - kScreenSize);
        renderY = std::clamp(renderY, 0, artwork.scleraHeight - kScreenSize);
    }

    int trackingUpper = 0;
    int trackingLower = 0;
    if (!trackingCurrentFrame_ || lastTrackingUpdateUs_ != nowUs) {
        trackingCurrentFrame_ = true;
        lastTrackingUpdateUs_ = nowUs;
        if (settings.tracking) {
            const std::uint8_t* upper =
                settings.symmetricalEyelid ? artwork.upperSymmetric : artwork.upperAsymmetric;
            int sampleX = artwork.scleraWidth / 2 - (renderX / 2);
            int sampleY = artwork.scleraHeight / 2 - (renderY + artwork.irisHeight / 4);
            sampleX = std::clamp(sampleX, 0, kScreenSize - 1);
            int sampleThreshold = 0;
            if (sampleY >= 0) {
                sampleY = std::clamp(sampleY, 0, kScreenSize - 1);
                sampleThreshold = (static_cast<int>(upper[sampleY * kScreenSize + sampleX]) +
                                   static_cast<int>(upper[sampleY * kScreenSize + (kScreenSize - 1 - sampleX)])) / 2;
            }
            trackingUpper_ = (trackingUpper_ * 3 + sampleThreshold) / 4;
            trackingLower_ = 254 - trackingUpper_;
        } else {
            trackingUpper_ = 0;
            trackingLower_ = 0;
        }
    }
    trackingUpper = trackingUpper_;
    trackingLower = trackingLower_;

    if (mode_ == AnimationMode::Dozing) {
        // Drowsiness-driven eyelid thresholds.
        const float leftPos = drowsiness_.leftEyelidPosition();   // 0=open, 1=closed
        const float rightPos = drowsiness_.rightEyelidPosition();

        // Map 0..1 to threshold values: 0 (open) = trackingUpper, 1 (closed) = 254.
        const int leftUpper = static_cast<int>(
            trackingUpper + (254 - trackingUpper) * leftPos);
        const int rightUpper = static_cast<int>(
            trackingUpper + (254 - trackingUpper) * rightPos);
        const int leftLower = static_cast<int>(
            trackingLower - trackingLower * leftPos);
        const int rightLower = static_cast<int>(
            trackingLower - trackingLower * rightPos);

        if (eyeIndex == 0) {
            state.upperThreshold = std::clamp(leftUpper, 0, 255);
            state.lowerThreshold = std::clamp(leftLower, 0, 255);
        } else {
            state.upperThreshold = std::clamp(rightUpper, 0, 255);
            state.lowerThreshold = std::clamp(rightLower, 0, 255);
        }
        state.dozeClosure = static_cast<int>(
            std::max(leftPos, rightPos) * 255.0F);

        // Drowsiness reduces iris scale (pupils dilate).
        const float d = drowsiness_.drowsiness();
        const int baseIris = 420 - static_cast<int>(180.0F * d);
        int combinedIris = std::clamp(baseIris, artwork.irisMin, artwork.irisMax);

        if (environmentalInitialized_ && environmentalIrisWeight_ > 0.0F) {
            const float environmentalScale = artwork.irisMax -
                (artwork.irisMax - artwork.irisMin) * currentPupil_;
            const float autonomousScale = std::clamp(
                static_cast<float>(combinedIris),
                static_cast<float>(artwork.irisMin),
                static_cast<float>(artwork.irisMax));
            combinedIris = std::lround(autonomousScale +
                (environmentalScale - autonomousScale) * environmentalIrisWeight_);
            combinedIris = std::clamp(combinedIris, artwork.irisMin, artwork.irisMax);
        }

        EyeFrame frame;
        frame.render = renderer.render(renderX, renderY, combinedIris, state, settings);
        frame.gazeX = eyeX;
        frame.gazeY = eyeY;
        frame.renderX = renderX;
        frame.renderY = renderY;
        frame.irisScale = combinedIris;
        frame.environmentalPupil = environmentalInitialized_ ? targetPupil_ : 0.0F;
        frame.currentPupil = environmentalInitialized_ ? currentPupil_ : 0.0F;
        frame.blinkState = 0;
        frame.upperThreshold = state.upperThreshold;
        frame.lowerThreshold = state.lowerThreshold;
        frame.dozeClosure = state.dozeClosure;
        return frame;

    } else if (blinkState_.state != kNoBlink) {
        const std::uint64_t elapsed = nowUs - blinkState_.startTimeUs;
        std::uint32_t position = elapsed >= blinkState_.durationUs
                                     ? 255
                                     : static_cast<std::uint32_t>(
                                           255 * elapsed / blinkState_.durationUs);
        if (blinkState_.state == kOpening) {
            position = 1 + position;
        } else {
            position = 256 - position;
        }
        state.upperThreshold = static_cast<int>(
            (static_cast<std::uint64_t>(trackingUpper) * position +
             static_cast<std::uint64_t>(254) * (257 - position)) / 256);
        state.lowerThreshold = static_cast<int>(
            (static_cast<std::uint64_t>(trackingLower) * position +
             static_cast<std::uint64_t>(254) * (257 - position)) / 256);
        if (elapsed >= blinkState_.durationUs) {
            if (blinkState_.state == kClosing) {
                blinkState_.state = kOpening;
                blinkState_.durationUs *= 2;
                blinkState_.startTimeUs = nowUs;
            } else {
                blinkState_.state = kNoBlink;
                blinkState_.startTimeUs = 0;
                blinkState_.durationUs = 0;
            }
        }
    } else {
        state.upperThreshold = trackingUpper;
        state.lowerThreshold = trackingLower;
        state.dozeClosure = 0;
    }

    state.blinkState = blinkState_.state;
    state.blinkStartTimeUs = blinkState_.startTimeUs;
    state.blinkDurationUs = blinkState_.durationUs;

    const int requestedIris = (irisScale != 0 ? irisScale : irisScale_);
    int combinedIris = std::clamp(requestedIris, artwork.irisMin, artwork.irisMax);
    if (environmentalInitialized_ && environmentalIrisWeight_ > 0.0F) {
        const float environmentalScale = artwork.irisMax -
            (artwork.irisMax - artwork.irisMin) * currentPupil_;
        const float autonomousScale = std::clamp(
            static_cast<float>(combinedIris),
            static_cast<float>(artwork.irisMin),
            static_cast<float>(artwork.irisMax));
        combinedIris = std::lround(autonomousScale +
            (environmentalScale - autonomousScale) * environmentalIrisWeight_);
        combinedIris = std::clamp(combinedIris, artwork.irisMin, artwork.irisMax);
    }
    EyeFrame frame;
    frame.render = renderer.render(renderX, renderY, combinedIris, state, settings);
    frame.gazeX = eyeX;
    frame.gazeY = eyeY;
    frame.renderX = renderX;
    frame.renderY = renderY;
    frame.irisScale = combinedIris;
    frame.environmentalPupil = environmentalInitialized_ ? targetPupil_ : 0.0F;
    frame.currentPupil = environmentalInitialized_ ? currentPupil_ : 0.0F;
    frame.blinkState = state.blinkState;
    frame.upperThreshold = state.upperThreshold;
    frame.lowerThreshold = state.lowerThreshold;
    frame.dozeClosure = state.dozeClosure;
    return frame;
}

}  // namespace uncanny
