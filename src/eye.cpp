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

// Normalized alertness gaze offsets are -1..1; this is the coordinate span they
// map onto, so a full dart moves the eye a quarter of the way to the limit.
constexpr float kGazeSpan = 256.0F;

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

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

// Smoothstep, used for eyelid closure. A lid does not snap shut and snap open.
float smoothStep(float t) {
    const float clamped = clampUnit(t);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

// Cosine quarter-wave: 0 at t=0, 1 at t=1, with a gentle S-curve that rounds
// the corners of the caruncle fade rather than producing a linear ramp.
float cosFade(float t) {
    const float clamped = clampUnit(t);
    return 1.0F - std::cos(clamped * static_cast<float>(3.14159265358979323846) / 2.0F);
}

}  // namespace

// Convert normalized dilation (0..1) to pupil-to-limbus ratio using calibration
float dilationToPupilRatio(float dilation, const EyeCalibration& calibration) {
    const float D = std::clamp(dilation, 0.0F, 1.0F);
    return calibration.pupilRatioMin + D * (calibration.pupilRatioMax - calibration.pupilRatioMin);
}

// Calculate pupil radius from calibrated ratio model
float pupilRadiusFromCalibration(float dilation, const EyeCalibration& calibration) {
    const float pupilRatio = dilationToPupilRatio(dilation, calibration);
    const float pupilDiameter = calibration.limbusDiameter * pupilRatio;
    return pupilDiameter / 2.0F;
}

// Backward compatibility function using old irisScale-based threshold
float dilationFromIrisScale(int irisScale) {
    // Convert irisScale (0..1023) to normalized dilation (0..1)
    // Higher irisScale = more constricted = smaller pupil = lower dilation
    return 1.0F - static_cast<float>(irisScale) / 1023.0F;
}

PupilGeometry pupilGeometry(int irisScale, int pupilShape, float pupilRatio,
                            int irisHeight, const EyeCalibration& calibration) {
    PupilGeometry geometry;
    geometry.irisRadius = static_cast<double>(irisHeight) / 2.0;
    
    // NEW: Use calibrated ratio model
    const float dilation = dilationFromIrisScale(irisScale);
    geometry.radius = static_cast<double>(pupilRadiusFromCalibration(dilation, calibration));
    
    // NEW: Replace irisThreshold with calibrated radius logic
    // The iris stops where the pupil starts: we need the threshold that produces
    // the calibrated pupil radius using the existing polar sampling formula.
    // The relationship is: radius = (127 - irisThreshold) / 128 * irisRadius
    // Therefore: irisThreshold = 127 - 128 * radius / irisRadius
    geometry.irisThreshold = static_cast<int>(127.0 - 128.0 * geometry.radius / geometry.irisRadius);

    const double ratio = std::clamp(static_cast<double>(pupilRatio), 0.01, 1.0);
    if (pupilShape == 1 || pupilShape == 2) {
        // A slit shortens one axis only, and never past the round pupil it
        // replaces, so the pupil stays inside the iris disc by construction.
        geometry.semiMinor = geometry.radius * ratio;
        geometry.semiMajor = geometry.radius;
    } else {
        geometry.semiMinor = geometry.radius;
        geometry.semiMajor = geometry.radius;
    }

    return geometry;
}

// ── Renderer ───────────────────────────────────────────────────────────────
// Deliberately mode-blind. Every behavioural decision arrives pre-baked in
// RenderInput; the only artwork-specific behaviour is the default design's
// caruncle fade, which is a property of that artwork rather than of a mode.
EyeRenderer::EyeRenderer(const EyeArtwork& artwork) : artwork_(artwork) {}

EyeRender EyeRenderer::render(const RenderInput& input) const {
    EyeRender output;

    const int irisThreshold = (128 * (1023 - input.irisScale) + 512) / 1024;
    const std::uint32_t irisScaleFixed =
        static_cast<std::uint32_t>(artwork_.irisMapHeight) * 65536U /
        static_cast<std::uint32_t>(std::max(1, irisThreshold));

    const int scleraRangeX = std::max(0, artwork_.scleraWidth - kScreenSize);
    const int scleraRangeY = std::max(0, artwork_.scleraHeight - kScreenSize);
    const int scleraX = std::clamp(input.gazeX, 0, scleraRangeX);
    const int scleraY = std::clamp(input.gazeY, 0, scleraRangeY);

    const std::uint8_t* upper =
        input.symmetricalEyelid ? artwork_.upperSymmetric : artwork_.upperAsymmetric;
    const std::uint8_t* lower =
        input.symmetricalEyelid ? artwork_.lowerSymmetric : artwork_.lowerAsymmetric;
    const std::uint8_t upperThreshold =
        std::clamp(input.upperThreshold, 0, 255);
    const std::uint8_t lowerThreshold =
        std::clamp(input.lowerThreshold, 0, 255);

    const bool isDefaultEye = (artwork_.name == "default");
    const int lidFade = std::clamp(input.lidFade, 0, 255);

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
                    // irisThreshold. What differs is how the pupil is carved
                    // out of it: by default the radial distance field carves a
                    // round pupil, but that field is radial by construction and
                    // cannot express a slit, so a shaped pupil replaces the
                    // round test outright rather than adding to it.
                    //
                    // TODO B5: the slit is measured from the same normalized
                    // pupil radius as the round case and is always strictly
                    // inside the iris disc, so the pupil can never render
                    // outside the dark region at any dilation.
                    const double centre = artwork_.irisHeight / 2.0;
                    const double dx = irisX + 0.5 - centre;
                    const double dy = irisY + 0.5 - centre;
                    const double rho2 = dx * dx + dy * dy;
                    // The iris can only exist inside the polar circle, which is
                    // what bounds it -- NOT the threshold. The threshold sets
                    // how much of that circle is iris, i.e. where the pupil
                    // starts.
                    const bool withinIris =
                        irisThreshold > 0 && rho2 <= centre * centre;

                    bool drawIris = false;
                    if (input.pupilShape != 0) {
                        bool inSlit = false;
                        if (withinIris) {
                            // The shared geometry helper is what the regression
                            // suite asserts against, so the slit cannot be
                            // wider than the round pupil it replaces and can
                            // never render outside the iris disc.
                            const PupilGeometry geometry =
                                pupilGeometry(input.irisScale, input.pupilShape,
                                              input.pupilRatio, artwork_.irisHeight,
                                              input.calibration);
                            if (geometry.radius > 0.5 &&
                                geometry.semiMinor > 0.0 && geometry.semiMajor > 0.0) {
                                // Semi-axes are oriented on the long axis; a
                                // vertical slit is narrow in x, a horizontal
                                // slit is narrow in y.
                                const bool vertical = input.pupilShape == 1;
                                const double ax = vertical ? geometry.semiMinor
                                                           : geometry.semiMajor;
                                const double ay = vertical ? geometry.semiMajor
                                                           : geometry.semiMinor;
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
                (static_cast<std::size_t>(y) * kScreenSize + x) * 4;

            std::uint32_t finalRgba = rgba;
            // ── Medial caruncle and rounded lid corners ───────────────────
            //
            // The original 128×128 eyes were authored for round TFT displays
            // whose circular screen border served as the caruncle frame — the
            // corners were physically hidden by the glass. On the flatscreen
            // window the full 128×128 eye is visible, so the lid edges at the
            // inner (medial) corner must provide their own soft, rounded
            // transition rather than a hard pixel cutoff.
            //
            // Both X-edges receive the same rounded treatment: the renderer is
            // per-eye and does not know which side is medial, so both corners
            // are rounded symmetrically. The display-layer horizontal flip
            // places each corner at the correct anatomical side.
            //
            // A cosine falloff (cosFade) replaces the old linear ramp so the
            // lid edge curves gently at the caruncle instead of ramping straight
            // off the pixel. The fade is always present — the flatscreen has no
            // screen border to hide behind. Closure deepens the caruncle shadow
            // but does not gate it on or off.
            if (isDefaultEye) {
                int cornerDist = 0;
                if (x < 8) cornerDist = 8 - x;
                else if (x > kScreenSize - 8) cornerDist = x - (kScreenSize - 8);
                if (cornerDist > 0) {
                    const float distNorm =
                        static_cast<float>(cornerDist) / 8.0F;
                    const float rounded = cosFade(distNorm);
                    const float closure =
                        static_cast<float>(lidFade) / 255.0F;
                    const float strength = 0.10F + 0.10F * closure;
                    const float alpha = 1.0F - strength * rounded;
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

// ── Animator ───────────────────────────────────────────────────────────────

EyeAnimator::EyeAnimator(AnimationSettings settings)
    : settings_(settings),
      pupilResponse_([settings] {
          PupilResponseConfig config;
          config.constrictionTime = settings.constrictionTime;
          config.dilationTime = settings.dilationTime;
          return config;
      }()),
      randomState_(static_cast<std::uint32_t>(settings.gazeSeed)) {
    if (randomState_ == 0) {
        randomState_ = 0x6d2b79f5U;
    }
    rebuildProfile();
}

void EyeAnimator::rebuildProfile() {
    // The profile owns the dilation envelope. Hardcoding the artwork range
    // back over the top of it here would silently undo the iris classification
    // (TODO B4): every IrisType would end up with identical travel.
    ExpressionProfile built = buildProfile(mode_, irisType_, pupilType_, realism_);

    // Expression intensity is a single operator-facing multiplier (TODO C3) so
    // the TUI can dial the whole performance up or down without touching any
    // individual channel.
    const float intensity = std::max(0.0F, expressionIntensity_);
    built.lidDroop = clampUnit(built.lidDroop * intensity);
    built.lidAsymmetry = clampUnit(built.lidAsymmetry * intensity);
    built.agitation = clampUnit(built.agitation * intensity);

    profile_ = built;

    AlertnessConfig alertness;
    alertness.enabled = (mode_ == AnimationMode::Suspicious ||
                         mode_ == AnimationMode::Angry);
    alertness.fixationStrength = profile_.fixationStrength;
    alertness.dartAmplitude = std::max(0.05F, 0.45F * (1.0F - profile_.fixationStrength));
    alertness.dartIntervalMin = 1.6F;
    alertness.dartIntervalMax = 4.2F;
    alertness.dartHoldTime = 0.30F;
    alertness.lidNarrow = profile_.lidDroop;
    alertness.agitation = profile_.agitation;
    alertness_ = AlertnessModel(alertness);

    // The resting pupil is a profile value, not a sticky piece of state. Without
    // this anchor, irisScale_ -- which is otherwise written only by
    // setIrisScale() -- would keep serving the previous mode's pupil forever,
    // so a mode or classification change would visibly keep the old size. This
    // is what makes the per-mode irisScaleBase take effect for every mode, not
    // just dozing. An explicit iris-scale override (debug iris, --iris-scale)
    // is a stronger signal than mode logic, so it is set afterwards and must
    // survive later rebuilds.
    if (!irisScaleOverride_) {
        irisScale_ = std::clamp(profile_.irisScaleBase, profile_.irisScaleMin,
                                profile_.irisScaleMax);
    }
}

void EyeAnimator::setEyeTypes(IrisType irisType, Realism realism) {
    irisType_ = irisType;
    realism_ = realism;
    rebuildProfile();
}

void EyeAnimator::setPupilType(PupilType pupilType) {
    pupilType_ = pupilType;
    pupilTypeExplicit_ = true;
    rebuildProfile();
}

void EyeAnimator::setExpressionIntensity(float intensity) {
    expressionIntensity_ = intensity;
    rebuildProfile();
}

void EyeAnimator::setLdrSensitivity(float sensitivity) {
    ldrSensitivity_ = std::max(0.0F, sensitivity);
}

void EyeAnimator::setMode(AnimationMode mode, std::uint64_t nowUs) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    rebuildProfile();
    // A mode change is a behavioural discontinuity: the shared clocks that
    // drive eyelid motion must not be carried across it, or the new mode
    // starts mid-blink with stale thresholds.
    lastFrameUs_ = ~std::uint64_t(0);
    lastTrackingUpdateUs_ = ~std::uint64_t(0);
    blink_ = BlinkState{};
    nextBlinkUs_ = 0;
    if (mode == AnimationMode::Dozing) {
        drowsiness_.reset();
        // TODO A4: dozing must be visibly dozing on the first frame after a
        // fresh boot. The baseline was left at 0.0 by default, which made
        // dozing take tens of seconds of accumulation before it was visible at
        // all. Seed a moderate starting drowsiness when the operator has not
        // asked for a specific baseline.
        if (drowsiness_.config().baseline == 0.0F) {
            drowsiness_.setDrowsiness(0.32F);
        }
    } else {
        // Leaving dozing must visibly recover rather than snapping wide open.
        alertness_.reset();
    }
    (void)nowUs;
}

void EyeAnimator::blink(std::uint64_t nowUs) {
    if (blink_.state != kNoBlink) {
        return;  // Already blinking — return early
    }
    blink_.state = kClosing;
    blink_.startTimeUs = nowUs;
    const float span = profile_.blinkDurationMax - profile_.blinkDurationMin;
    const float seconds = profile_.blinkDurationMin + span * randomUnit();
    blink_.durationUs = static_cast<std::uint64_t>(std::max(0.01F, seconds) * 1'000'000.0F);
    nextBlinkUs_ = nowUs + nextBlinkDelayUs();
}

std::uint64_t EyeAnimator::nextBlinkDelayUs() {
    // Blink suppression stretches the interval rather than dropping blinks
    // outright, so a suppressed eye still blinks -- just far less often, which
    // reads as a hard stare instead of a dead eye.
    const float suppression = clampUnit(profile_.blinkSuppression);
    const float scale = 1.0F / std::max(0.05F, 1.0F - suppression);
    const float span = profile_.blinkIntervalMax - profile_.blinkIntervalMin;
    float seconds = profile_.blinkIntervalMin + span * randomUnit();
    seconds *= scale;
    if (seconds < 0.1F) {
        seconds = 0.1F;
    }
    return static_cast<std::uint64_t>(seconds * 1'000'000.0F);
}

void EyeAnimator::setGaze(std::size_t /*eyeIndex*/, int x, int y) {
    // The gaze target is shared by both eyes: they are two views of one
    // subject, not two independent subjects.
    gaze_.baseX = gaze_.currentX;
    gaze_.baseY = gaze_.currentY;
    gaze_.newX = std::clamp(x, 0, kCoordinateMax);
    gaze_.newY = std::clamp(y, 0, kCoordinateMax);
    gaze_.inMotion = true;
    // A zero start and duration means the movement is due to have completed by
    // the next frame, which is what makes an explicit setGaze() take effect
    // immediately instead of lingering as a pending target.
    gaze_.moveStartTimeUs = 0;
    gaze_.moveDurationUs = 0;
}

void EyeAnimator::setIrisScale(int scale) {
    // Clamp to the mode's physiological range, not the raw 0..1023. The raw
    // range would allow the pupil to range from 100% of the iris (a black
    // disc filling the entire eye) to 0% (a pinprick), neither of which is
    // anatomically plausible for a human eye whose pupil dilates to roughly
    // 3mm–8mm (≈ 25%–67% of the limbus). The profile's irisScaleMin/Max are
    // the mode-specific bounds derived from the iris classification.
    irisScale_ = std::clamp(scale, profile_.irisScaleMin, profile_.irisScaleMax);
    irisScaleOverride_ = true;
}

void EyeAnimator::setEnvironmentalPupil(float pupil, std::uint64_t nowUs) {
    // LDR sensitivity warps the incoming target around mid-scale rather than
    // scaling it, so raising sensitivity widens the response around the
    // midpoint instead of saturating both ends immediately.
    const float base = clampUnit(pupil);
    const float sensitivity = std::max(0.0F, ldrSensitivity_);
    environmentalTarget_ = clampUnit(0.5F + (base - 0.5F) * sensitivity);
    if (!environmentalActive_) {
        environmentalActive_ = true;
        pupilResponse_.reset(environmentalTarget_);
    }
    pupilResponse_.update(environmentalTarget_, true, nowUs);
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

float EyeAnimator::randomUnit() {
    return static_cast<float>(random()) / static_cast<float>(0xFFFFFFFFU);
}

void EyeAnimator::beginIrisMotion(std::uint64_t nowUs) {
    oldIris_ = irisScale_;
    const int low = profile_.irisScaleMin;
    const int high = profile_.irisScaleMax;
    newIris_ = static_cast<int>(random(static_cast<std::uint32_t>(low),
                                       static_cast<std::uint32_t>(std::max(low, high))));
    irisStartUs_ = nowUs;
    const float span = profile_.irisSweepMax - profile_.irisSweepMin;
    float seconds = profile_.irisSweepMin + span * randomUnit();
    if (seconds < 0.1F) {
        seconds = 0.1F;
    }
    irisDurationUs_ = static_cast<std::int64_t>(seconds * 1'000'000.0F);
}

void EyeAnimator::updateBlink(std::uint64_t nowUs) {
    // The state machine is driven by absolute time, so a long frame gap has to
    // be caught up in one go. Advancing a single phase per call left a blink
    // stuck fully closed whenever the gap spanned the whole blink, and
    // advancing the phase boundary to `nowUs` instead of the real boundary
    // would report the eye as freshly closed on the very frame it reopened.
    constexpr int kMaxPhaseCatchUp = 4;  // close, open, close, open
    for (int step = 0; step < kMaxPhaseCatchUp && blink_.state != kNoBlink; ++step) {
        if (nowUs < blink_.startTimeUs) {
            return;
        }
        const std::uint64_t elapsed = nowUs - blink_.startTimeUs;
        if (elapsed < blink_.durationUs) {
            return;
        }
        const std::uint64_t boundary = blink_.startTimeUs + blink_.durationUs;
        if (blink_.state == kClosing) {
            blink_.state = kOpening;
            // Eyes reopen more slowly than they close; the upstream sketch
            // doubles the closing duration for the opening phase.
            blink_.durationUs *= 2;
            blink_.startTimeUs = boundary;
        } else {
            blink_.state = kNoBlink;
            blink_.startTimeUs = 0;
            blink_.durationUs = 0;
        }
    }
}

void EyeAnimator::maybeScheduleBlink(std::uint64_t nowUs) {
    if (blink_.state != kNoBlink) {
        return;
    }
    if (!settings_.autonomousBlink) {
        return;
    }
    if (nowUs >= nextBlinkUs_) {
        blink(nowUs);
    }
}

// Dozing fatigue factors, all keyed to drowsiness d (0 alert .. 1 fighting
// sleep). They describe the physiology of a tiring gaze: movements get
// lazier, the eye holds a position far longer between shifts, and the landing
// gets sloppier. Outside dozing every one of them is 1 / 0.
float EyeAnimator::dozingSaccadeSlowdown() const {
    if (mode_ != AnimationMode::Dozing) {
        return 1.0F;
    }
    return 1.0F + 2.0F * clampUnit(drowsiness_.drowsiness());
}

float EyeAnimator::dozingHoldStretch() const {
    if (mode_ != AnimationMode::Dozing) {
        return 1.0F;
    }
    return 1.0F + 1.5F * clampUnit(drowsiness_.drowsiness());
}

float EyeAnimator::dozingLandingError() const {
    if (mode_ != AnimationMode::Dozing) {
        return 0.0F;
    }
    return 10.0F + 40.0F * clampUnit(drowsiness_.drowsiness());
}

// Per-eye vergence appearance. Runs once per eye per frame, but only while
// dozing: in every other mode the two eyes move as one, exactly as the shared
// gaze contract requires.
void EyeAnimator::updateVergence(std::size_t eyeIndex, std::uint64_t nowUs) {
    const std::size_t eye = eyeIndex < kEyeCount ? eyeIndex : 0;
    if (mode_ != AnimationMode::Dozing) {
        vergeDriftX_[eye] = 0.0F;
        vergeDriftY_[eye] = 0.0F;
        vergeOffsetX_[eye] = 0.0F;
        vergeOffsetY_[eye] = 0.0F;
        vergeDriftEnv_[eye] = 0.0F;
        vergeDriftEpisodeUs_[eye] = 0;
        vergeDriftRestUs_[eye] = 0;
        vergeMicroX_[eye] = 0.0F;
        vergeMicroY_[eye] = 0.0F;
        vergeMicroTimerUs_[eye] = 0;
        vergeLastUs_[eye] = nowUs;
        return;
    }

    const float d = clampUnit(drowsiness_.drowsiness());
    float dt = 0.0F;
    if (vergeLastUs_[eye] != 0 && nowUs >= vergeLastUs_[eye]) {
        dt = static_cast<float>(nowUs - vergeLastUs_[eye]) / 1'000'000.0F;
    }
    vergeLastUs_[eye] = nowUs;
    if (dt <= 0.0F || dt > 0.25F) {
        dt = 0.0F;  // first frame or a stall: nothing to integrate
    }

    // ── Persistent convergence error ──────────────────────────────────────
    // A very slow per-eye walk (multi-second time constant, pixel-scale
    // bound). This is the vergence read proper: the two eyes sit slightly
    // differently aimed and that misalignment evolves over seconds, which is
    // what a fatigued pair looks like. It is deliberately too slow to read as
    // motion -- the shakiness complaint was about anything that moved every
    // frame, and this never does.
    if (dt > 0.0F) {
        const float amp = 15.0F + 45.0F * d;
        const float step = (10.0F + 30.0F * d) * dt;
        vergeOffsetX_[eye] += (randomUnit() - 0.5F) * 2.0F * step;
        vergeOffsetY_[eye] += (randomUnit() - 0.5F) * 2.0F * step;
        const float reversion = std::exp(-dt / (5.0F + 3.0F * d));
        vergeOffsetX_[eye] *= reversion;
        vergeOffsetY_[eye] *= reversion;
        vergeOffsetX_[eye] = std::clamp(vergeOffsetX_[eye], -amp, amp);
        vergeOffsetY_[eye] = std::clamp(vergeOffsetY_[eye], -amp, amp);
    }

    // ── Fixation instability: intermittent drift episodes ─────────────────
    // NOT a continuously running walk -- an eye that moves every single frame
    // reads as a shake, not as fatigue. Real fixation loss comes in episodes:
    // the eye holds still, slowly wanders off, then re-fixates and holds again.
    // Each eye runs its own episode schedule, which is what gives the pair its
    // intermittent divergence (the vergence read). vergeDriftEnv_ carries the
    // episode envelope; the walk itself lives in vergeDriftX_/Y_.
    if (dt > 0.0F) {
        if (vergeDriftEpisodeUs_[eye] > 0) {
            // Mid-episode: integrate a small mean-reverting walk inside a
            // ramp-up / hold / ramp-down envelope, so the drift starts and
            // ends gently instead of snapping on and off.
            vergeDriftEpisodeUs_[eye] -= static_cast<std::int64_t>(dt * 1'000'000.0F);
            vergeDriftAgeUs_[eye] += static_cast<std::int64_t>(dt * 1'000'000.0F);
            const float progress = vergeDriftEpisodeLenUs_[eye] > 0
                ? 1.0F - static_cast<float>(vergeDriftEpisodeUs_[eye]) /
                             static_cast<float>(vergeDriftEpisodeLenUs_[eye])
                : 1.0F;
            float envelope = 1.0F;
            if (progress < 0.25F) {
                envelope = progress / 0.25F;
            } else if (progress > 0.75F) {
                envelope = (1.0F - progress) / 0.25F;
            }
            vergeDriftEnv_[eye] = std::clamp(envelope, 0.0F, 1.0F);

            const float amp = 8.0F + 20.0F * d;
            const float step = (25.0F + 70.0F * d) * dt;
            vergeDriftX_[eye] += (randomUnit() - 0.5F) * 2.0F * step;
            vergeDriftY_[eye] += (randomUnit() - 0.5F) * 2.0F * step;
            const float reversion = std::exp(-dt / (1.8F + 1.5F * d));
            vergeDriftX_[eye] *= reversion;
            vergeDriftY_[eye] *= reversion;
            vergeDriftX_[eye] = std::clamp(vergeDriftX_[eye], -amp, amp);
            vergeDriftY_[eye] = std::clamp(vergeDriftY_[eye], -amp, amp);

            if (vergeDriftEpisodeUs_[eye] <= 0) {
                // Episode over: re-fixate, then hold still. A tired eye holds
                // still for LESS time between episodes than an alert one.
                const float rest =
                    1.2F + (1.0F - d) * randomUnit() * 2.5F + 0.6F * randomUnit();
                vergeDriftRestUs_[eye] =
                    static_cast<std::int64_t>(rest * 1'000'000.0F);
            }
        } else {
            // Resting: settle back to the fixation and hold perfectly still.
            vergeDriftX_[eye] *= std::exp(-dt / 0.35F);
            vergeDriftY_[eye] *= std::exp(-dt / 0.35F);
            vergeDriftEnv_[eye] = std::max(0.0F, vergeDriftEnv_[eye] - dt / 0.35F);
            vergeDriftRestUs_[eye] -= static_cast<std::int64_t>(dt * 1'000'000.0F);
            if (vergeDriftRestUs_[eye] <= 0) {
                const float seconds = 1.0F + (0.6F + 1.6F * d) * randomUnit();
                vergeDriftEpisodeLenUs_[eye] =
                    static_cast<std::int64_t>(seconds * 1'000'000.0F);
                vergeDriftEpisodeUs_[eye] = vergeDriftEpisodeLenUs_[eye];
                vergeDriftAgeUs_[eye] = 0;
            }
        }

        // Active microsaccade impulses decay over ~70 ms. They fire only while
        // the eye is actually transiting between gaze points -- running them
        // through a fixation reads as nervous twitching rather than as the
        // texture of a movement -- and they are deliberately small now: a
        // fraction of a pixel, enough to keep a tired saccade from looking
        // mechanically smooth.
        if (gaze_.inMotion && vergeMicroTimerUs_[eye] <= 0) {
            const float lo = 0.14F;
            const float hi = std::max(lo, 0.40F - 0.20F * d);
            vergeMicroTimerUs_[eye] = static_cast<std::int64_t>(
                (lo + (hi - lo) * randomUnit()) * 1'000'000.0F);
        } else if (vergeMicroTimerUs_[eye] > 0) {
            vergeMicroTimerUs_[eye] -= static_cast<std::int64_t>(dt * 1'000'000.0F);
        }
        const float decay = std::exp(-dt / 0.07F);
        vergeMicroX_[eye] *= decay;
        vergeMicroY_[eye] *= decay;
        if (gaze_.inMotion && vergeMicroTimerUs_[eye] <= 0) {
            const float mag = 2.0F + 6.0F * d;
            vergeMicroX_[eye] = (randomUnit() - 0.5F) * 2.0F * mag;
            vergeMicroY_[eye] = (randomUnit() - 0.5F) * 2.0F * mag;
        }
    }
}

// Dozing nod-off gaze choreography.
//
// While the lids are down for a micro-sleep the shared gaze rolls slowly off
// its fixation (eyes drift when they close -- invisible behind the lids). On
// waking, the gaze SNAPS back to the position it held when the nod-off began,
// rather than resuming the wander from wherever the drift left it. If the
// model rolled an attention check, a lateral glance follows -- a look to one
// side and back, the "did anybody notice" -- and only then does ordinary
// wandering resume.
void EyeAnimator::applyDozingNodOffGaze(std::uint64_t nowUs) {
    const DrowsinessState& drowsy = drowsiness_.state();
    const DrowsinessEvent event = drowsy.currentEvent;

    // ── Phase 1: lids shut for the nod-off ────────────────────────────────
    // The shared gaze rolls slowly off its fixation while the eyes are closed
    // (real eyes drift when they shut -- invisible behind the lids, but it is
    // what the wake-up snaps back from).
    if (event == DrowsinessEvent::MicroSleep) {
        if (!nodOffActive_) {
            nodOffActive_ = true;
            nodOffGazeX_ = gaze_.currentX;
            nodOffGazeY_ = gaze_.currentY;
            // The roll direction is random, but with a floor on its size: the
            // point of the wake-up snap-back is that there is always something
            // to snap back FROM, even after a short nod-off.
            const float rollX = (randomUnit() - 0.5F) * 200.0F;
            const float rollY = (randomUnit() - 0.5F) * 140.0F;
            nodOffRollX_ = std::copysign(std::max(60.0F, std::fabs(rollX)), rollX);
            nodOffRollY_ = std::copysign(std::max(40.0F, std::fabs(rollY)), rollY);
            nodOffRollProgressX_ = 0.0F;
            nodOffRollProgressY_ = 0.0F;
            gaze_.inMotion = false;
        }
        float dtUs = 0.0F;
        if (nodOffLastUs_ != 0 && nowUs >= nodOffLastUs_) {
            dtUs = static_cast<float>(nowUs - nodOffLastUs_);
        }
        nodOffLastUs_ = nowUs;
        if (dtUs > 0.0F && dtUs < 250'000.0F) {
            const float step = 120.0F / 1'000'000.0F * dtUs;
            nodOffRollProgressX_ = std::clamp(
                nodOffRollProgressX_ + std::copysign(step, nodOffRollX_),
                std::min(0.0F, nodOffRollX_), std::max(0.0F, nodOffRollX_));
            nodOffRollProgressY_ = std::clamp(
                nodOffRollProgressY_ + std::copysign(step, nodOffRollY_),
                std::min(0.0F, nodOffRollY_), std::max(0.0F, nodOffRollY_));
        }
        gaze_.currentX = std::clamp(nodOffGazeX_ + static_cast<int>(nodOffRollProgressX_),
                                    0, kCoordinateMax);
        gaze_.currentY = std::clamp(nodOffGazeY_ + static_cast<int>(nodOffRollProgressY_),
                                    0, kCoordinateMax);
        gaze_.baseX = gaze_.currentX;
        gaze_.baseY = gaze_.currentY;
        nodOffSequenceActive_ = true;
        return;
    }

    if (nodOffActive_) {
        // Waking: remember where the roll left the gaze so the return can
        // start from there.
        nodOffActive_ = false;
        nodOffRollProgressX_ = 0.0F;
        nodOffRollProgressY_ = 0.0F;
        nodOffReturnFromX_ = gaze_.currentX;
        nodOffReturnFromY_ = gaze_.currentY;
        nodOffReturnStartUs_ = nowUs;
        nodOffReturning_ = true;
    }
    nodOffLastUs_ = nowUs;

    // ── Phase 2: snappy return to the remembered fixation ─────────────────
    // Restored over ~250 ms while the lids lift, so the eye re-establishes
    // where it was instead of resuming the wander from a drifted spot.
    if (nodOffReturning_) {
        const float t = std::clamp(
            static_cast<float>(nowUs - nodOffReturnStartUs_) / 250'000.0F, 0.0F, 1.0F);
        const float eased = 1.0F - (1.0F - t) * (1.0F - t);
        gaze_.currentX = std::clamp(
            nodOffReturnFromX_ + static_cast<int>(
                static_cast<float>(nodOffGazeX_ - nodOffReturnFromX_) * eased),
            0, kCoordinateMax);
        gaze_.currentY = std::clamp(
            nodOffReturnFromY_ + static_cast<int>(
                static_cast<float>(nodOffGazeY_ - nodOffReturnFromY_) * eased),
            0, kCoordinateMax);
        gaze_.baseX = gaze_.currentX;
        gaze_.baseY = gaze_.currentY;
        nodOffSequenceActive_ = true;
        if (t >= 1.0F) {
            nodOffReturning_ = false;
        }
        return;
    }

    // ── Phase 3: the lateral glance ───────────────────────────────────────
    // Out to one side and back across the attention-check window: the "did
    // anybody notice" look, ending on the remembered fixation.
    if (event == DrowsinessEvent::AttentionCheck && drowsy.attentionDirection != 0.0F) {
        const float p = std::clamp(drowsy.attentionProgress, 0.0F, 1.0F);
        const float shape = std::sin(p * 3.14159265F);
        const float amp = 240.0F * drowsy.attentionDirection;
        gaze_.currentX = std::clamp(nodOffGazeX_ + static_cast<int>(amp * shape),
                                    0, kCoordinateMax);
        gaze_.currentY = std::clamp(nodOffGazeY_, 0, kCoordinateMax);
        gaze_.baseX = gaze_.currentX;
        gaze_.baseY = gaze_.currentY;
        nodOffSequenceActive_ = true;
        return;
    }

    // ── Sequence over ─────────────────────────────────────────────────────
    // Restart the hold clock exactly once, on the transition out, so the
    // wander resumes on its own schedule rather than on the next frame. The
    // remembered fixation is restored exactly here: the glance's final frame
    // stops a hair short of centre, and "back where it was" means back where
    // it was.
    if (nodOffSequenceActive_) {
        nodOffSequenceActive_ = false;
        gaze_.inMotion = false;
        gaze_.currentX = nodOffGazeX_;
        gaze_.currentY = nodOffGazeY_;
        gaze_.baseX = nodOffGazeX_;
        gaze_.baseY = nodOffGazeY_;
        gaze_.moveStartTimeUs = nowUs;
        gaze_.moveDurationUs = static_cast<std::int64_t>(
            static_cast<double>(gazeHoldUs()) * dozingHoldStretch());
    }
}

void EyeAnimator::advanceGaze(std::uint64_t nowUs) {
    // While a dozing nod-off sequence owns the gaze (sleeping, waking, or
    // glancing sideways) the saccade machine stays parked. Without this the
    // machine keeps picking targets behind the choreography's back and the
    // wander resumes mid-glance.
    if (mode_ == AnimationMode::Dozing &&
        (nodOffReturning_ ||
         drowsiness_.currentEvent() == DrowsinessEvent::MicroSleep ||
         drowsiness_.currentEvent() == DrowsinessEvent::SleepRecovery ||
         drowsiness_.currentEvent() == DrowsinessEvent::AttentionCheck)) {
        gaze_.inMotion = false;
        return;
    }
    // An explicitly requested gaze must animate whether or not autonomous
    // wandering is enabled. Only the *picking of new random targets* is gated
    // on autonomousGaze: gating the easing too (as this used to) made
    // setGaze() a silent no-op with autonomousGaze off, so the eye stayed
    // parked on its stale position and never reached its target.
    const std::int64_t elapsed =
        nowUs >= gaze_.moveStartTimeUs
            ? static_cast<std::int64_t>(nowUs - gaze_.moveStartTimeUs)
            : 0;

    if (gaze_.inMotion) {
        if (elapsed >= gaze_.moveDurationUs) {
            // Movement complete: park at the target and hold it for the
            // profile's dwell interval. That interval -- how OFTEN the gaze
            // position changes -- is the only "minimal movement" control; how
            // far the eye travels is never restricted.
            gaze_.inMotion = false;
            gaze_.moveDurationUs = static_cast<std::int64_t>(
                static_cast<double>(gazeHoldUs()) * dozingHoldStretch());
            gaze_.moveStartTimeUs = nowUs;
            // Fatigue makes the landing inaccurate: an autonomous target is
            // parked with a drowsiness-scaled offset, and the next saccade
            // starts from that sloppy position. An explicitly requested
            // position (setGaze) is still hit exactly.
            const float error = dozingLandingError();
            if (gaze_.autonomous && error > 0.0F) {
                gaze_.baseX = std::clamp(
                    gaze_.newX + static_cast<int>((randomUnit() - 0.5F) * 2.0F * error),
                    0, kCoordinateMax);
                gaze_.baseY = std::clamp(
                    gaze_.newY + static_cast<int>((randomUnit() - 0.5F) * 2.0F * error),
                    0, kCoordinateMax);
            } else {
                gaze_.baseX = gaze_.newX;
                gaze_.baseY = gaze_.newY;
            }
            gaze_.currentX = gaze_.baseX;
            gaze_.currentY = gaze_.baseY;
        } else {
            const std::uint64_t denominator =
                static_cast<std::uint64_t>(gaze_.moveDurationUs);
            const std::size_t easeIndex =
                denominator == 0
                    ? 0
                    : std::min<std::uint64_t>(
                          255, 255 * static_cast<std::uint64_t>(elapsed) / denominator);
            const int interpolation = kEase[easeIndex] + 1;
            gaze_.currentX = gaze_.baseX + static_cast<int>(
                static_cast<std::int64_t>(gaze_.newX - gaze_.baseX) * interpolation / 256);
            gaze_.currentY = gaze_.baseY + static_cast<int>(
                static_cast<std::int64_t>(gaze_.newY - gaze_.baseY) * interpolation / 256);
        }
    } else if (settings_.autonomousGaze && elapsed > gaze_.moveDurationUs) {
        // New target anywhere inside the coordinate circle around centre. The
        // eye explores the full legal range: "minimal movement" is how often
        // the target changes (the profile's hold interval above), never a
        // bound on where the eye may look. Legal-position enforcement is the
        // artwork clamp in resolveGaze(), applied after the alertness offsets.
        int dx = 0;
        int dy = 0;
        do {
            gaze_.newX = static_cast<int>(random(0, 1024));
            gaze_.newY = static_cast<int>(random(0, 1024));
            dx = gaze_.newX * 2 - kCoordinateMax;
            dy = gaze_.newY * 2 - kCoordinateMax;
        } while (static_cast<std::int64_t>(dx) * dx +
                     static_cast<std::int64_t>(dy) * dy >
                 static_cast<std::int64_t>(kCoordinateMax) * kCoordinateMax);
        gaze_.baseX = gaze_.currentX;
        gaze_.baseY = gaze_.currentY;
        gaze_.autonomous = true;
        const float speed = std::max(0.1F, profile_.gazeSpeedScale);
        gaze_.moveDurationUs = static_cast<std::int64_t>(
            (72'000.0F + 72'000.0F * randomUnit()) / speed *
            dozingSaccadeSlowdown());
        gaze_.moveStartTimeUs = nowUs;
        gaze_.inMotion = true;
    }
}

std::int64_t EyeAnimator::gazeHoldUs() {
    const std::int64_t minUs = std::max<std::int64_t>(0, profile_.gazeHoldMinUs);
    const std::int64_t maxUs = std::max(minUs, profile_.gazeHoldMaxUs);
    // random(low, high) is inclusive on both ends, so the hold lands anywhere
    // in [gazeHoldMinUs, gazeHoldMaxUs].
    const std::uint32_t span = static_cast<std::uint32_t>(
        std::min<std::int64_t>(maxUs - minUs, 0xFFFFFFFE));
    return minUs + static_cast<std::int64_t>(random(0, span));
}

void EyeAnimator::resolveGaze(int& outGazeX, int& outGazeY) const {
    int eyeX = gaze_.currentX;
    int eyeY = gaze_.currentY;

    // Alertness adds a normalized offset on top, covering the saccades and the
    // fixated hold of the suspicious and angry stares.
    if (alertness_.config().enabled) {
        eyeX += static_cast<int>(alertness_.gazeOffsetX() * kGazeSpan);
        eyeY += static_cast<int>(alertness_.gazeOffsetY() * kGazeSpan);
    }

    // The ONLY gaze clamp: the legal-position bound. Everything above is
    // animation, and animation may put the pupil anywhere; this keeps the
    // crop inside the sclera artwork, which is the one illegal position.
    outGazeX = std::clamp(eyeX, 0, kCoordinateMax);
    outGazeY = std::clamp(eyeY, 0, kCoordinateMax);
}

float EyeAnimator::computeClosure(std::size_t eyeIndex, std::uint64_t nowUs) {
    const bool left = (eyeIndex == 0);

    // Every channel that can close an eye contributes a 0..1 closure and the
    // strongest one wins. Taking the maximum rather than summing is what keeps
    // a blink during an angry squint from over-closing past 1.0 and inverting
    // the threshold mapping.
    float closure = 0.0F;

    const float asymmetry =
        left ? profile_.lidAsymmetry : -profile_.lidAsymmetry;
    closure = std::max(closure, clampUnit(profile_.lidDroop + asymmetry));

    if (mode_ == AnimationMode::Dozing) {
        // DrowsinessModel owns the eyelids outright while dozing.
        closure = std::max(closure, left ? drowsiness_.leftEyelidPosition()
                                         : drowsiness_.rightEyelidPosition());
        return closure;
    }

    if (alertness_.config().enabled) {
        closure = std::max(closure, clampUnit(alertness_.lidNarrow()));
    }

    if (blink_.state == kClosing) {
        const float t = blink_.durationUs == 0
                            ? 1.0F
                            : static_cast<float>(nowUs - blink_.startTimeUs) /
                                  static_cast<float>(blink_.durationUs);
        closure = std::max(closure, smoothStep(t));
    } else if (blink_.state == kOpening) {
        const float t = blink_.durationUs == 0
                            ? 1.0F
                            : static_cast<float>(nowUs - blink_.startTimeUs) /
                                  static_cast<float>(blink_.durationUs);
        closure = std::max(closure, 1.0F - smoothStep(t));
    }

    return clampUnit(closure);
}

// The resting iris scale for the current mode, before the light response is
// blended in.
//
// Dozing deliberately drives the pupil from drowsiness rather than from the
// autonomous sweep, so the pupil tracks sleep pressure. The original code
// hardcoded `420 - 180 * drowsiness`; expressing it as the profile's base and
// range keeps the same numbers while letting the profile own them. The sign is
// `base + range * d` because a drowsy eye DILATES -- pupils do not shrink with
// sleepiness, and on this renderer a higher scale is a wider pupil.
//
// Every other mode rests at its own irisScaleBase -- the anchor in
// rebuildProfile() keeps irisScale_ equal to it, so the suspicious and angry
// profiles' constricted sizes are what actually renders.
int EyeAnimator::restingIrisScale(const EyeArtwork& artwork) const {
    if (irisScaleOverride_) {
        return std::clamp(irisScale_, artwork.irisMin, artwork.irisMax);
    }
    if (mode_ == AnimationMode::Dozing) {
        const float d = clampUnit(drowsiness_.drowsiness());
        const int base = profile_.irisScaleBase +
                         static_cast<int>(static_cast<float>(profile_.irisScaleRange) * d);
        return std::clamp(base, artwork.irisMin, artwork.irisMax);
    }
    return std::clamp(irisScale_, artwork.irisMin, artwork.irisMax);
}

int EyeAnimator::combineIris(int autonomousScale, const EyeArtwork& artwork) const {
    int combined = std::clamp(autonomousScale, artwork.irisMin, artwork.irisMax);
    if (!environmentalActive_ || settings_.environmentalIrisWeight <= 0.0F) {
        return combined;
    }

    // Pupil openness runs 0 (constricted, bright) to 1 (dilated, dark), and
    // irisScale runs the other way: irisMin is the most constricted and
    // irisMax the most dilated. Mapping openness linearly onto that range is
    // the whole of the light response here.
    const float openness = clampUnit(pupilResponse_.current());
    const float environmentalScale = static_cast<float>(artwork.irisMin) +
        static_cast<float>(artwork.irisMax - artwork.irisMin) * openness;
    const float autonomous = static_cast<float>(combined);
    const float blended = autonomous +
        (environmentalScale - autonomous) * settings_.environmentalIrisWeight;
    return std::clamp(static_cast<int>(std::lround(blended)), artwork.irisMin,
                      artwork.irisMax);
}

EyeFrame EyeAnimator::update(std::size_t eyeIndex, std::uint64_t nowUs,
                             const EyeArtwork& artwork,
                             const EyeRenderer& renderer,
                             const RenderSettings& settings) {
    if (eyeIndex >= kEyeCount) {
        eyeIndex = 0;
    }

    // ── Per-frame model updates, run exactly once regardless of eye index ──
    // Both eyes must observe an identical snapshot of the shared behaviour
    // models, or their eyelids can disagree on the same frame.
    const bool newFrame = lastFrameUs_ != nowUs;
    if (newFrame) {
        lastFrameUs_ = nowUs;
        if (mode_ == AnimationMode::Dozing) {
            drowsiness_.update(nowUs);
        }
        if (alertness_.config().enabled) {
            alertness_.update(nowUs);
        }
        pupilResponse_.update(environmentalTarget_, environmentalActive_, nowUs);
        updateBlink(nowUs);
        // While dozing, DrowsinessModel owns the eyelid entirely -- it
        // schedules the blinks, the partial droops and the micro-sleeps.
        // Scheduling animator blinks as well would put a second state machine
        // in charge of the same channel.
        if (mode_ != AnimationMode::Dozing) {
            maybeScheduleBlink(nowUs);
        }
        advanceGaze(nowUs);
        // The nod-off sequence (roll while shut, snap back on waking, lateral
        // glance) overrides the saccade machine while it runs. Outside dozing
        // it is a no-op.
        if (mode_ == AnimationMode::Dozing) {
            applyDozingNodOffGaze(nowUs);
        }

        // Autonomous iris sweeps are opt-in. With no light sensor attached
        // there is nothing that *should* move the pupil, and a self-driven
        // sweep reads as a pupil twitching for no reason. The resting pupil is
        // the mode's own value (see restingIrisScale); the sweep only adds
        // drift on top when the operator explicitly asks for it.
        if (settings_.autonomousIris) {
            // A sweep is armed when the previous one has run its course. The
            // end time is derived rather than stored so a mode change that
            // clears the clocks cannot leave a sweep wedged open.
            const std::uint64_t irisEndUs =
                irisStartUs_ + static_cast<std::uint64_t>(
                                   std::max<std::int64_t>(0, irisDurationUs_));
            if (irisDurationUs_ <= 0 || nowUs >= irisEndUs) {
                beginIrisMotion(nowUs);
            }
        }
    }

    // ── Autonomous iris position ──────────────────────────────────────────
    // When the sweep is off this stays at the mode's resting value, so the
    // pupil holds still unless something has a reason to move it. A direct
    // iris-scale override is a stronger signal than mode logic, so it freezes
    // the pupil at the exact requested extreme even during dozing.
    int autonomousIris = restingIrisScale(artwork);
    if (!irisScaleOverride_ && settings_.autonomousIris && irisDurationUs_ > 0) {
        const std::uint64_t elapsed = nowUs >= irisStartUs_ ? nowUs - irisStartUs_ : 0;
        const std::size_t easeIndex =
            irisDurationUs_ == 0
                ? 0
                : std::min<std::uint64_t>(
                      255, 255 * elapsed / static_cast<std::uint64_t>(irisDurationUs_));
        const int interpolation = kEase[easeIndex] + 1;
        autonomousIris = oldIris_ +
            static_cast<int>(static_cast<std::int64_t>(newIris_ - oldIris_) * interpolation / 256);
    }

    // ── Gaze ──────────────────────────────────────────────────────────────
    int eyeX = 512;
    int eyeY = 512;
    resolveGaze(eyeX, eyeY);

    int renderX = mapRange(eyeX, 0, kCoordinateMax, 0, artwork.scleraWidth - kScreenSize);
    if (eyeIndex == 1) {
        renderX = artwork.scleraWidth - kScreenSize - renderX;
    }
    renderX = std::clamp(renderX + 4, 0, artwork.scleraWidth - kScreenSize);
    int renderY = std::clamp(
        mapRange(eyeY, 0, kCoordinateMax, 0, artwork.scleraHeight - kScreenSize),
        0, artwork.scleraHeight - kScreenSize);

    // ── Vergence appearance (dozing fatigue) ──────────────────────────────
    // Each eye adds its own fixation drift and microsaccade impulses on top of
    // the shared gaze. Because the walks are independent, a tiring pair
    // intermittently aims at different places -- the read of vergence fatigue
    // -- without ever breaking the shared-gaze mirror. Every other mode gets
    // an exact zero overlay.
    //
    // The walks are in gaze units (0..kCoordinateMax); the crop origin is in
    // artwork pixels, so the overlay is scaled by the crop range before it is
    // added. Getting this wrong makes the overlay roughly 14x too large.
    updateVergence(eyeIndex, nowUs);
    const std::size_t vergeEye = eyeIndex < kEyeCount ? eyeIndex : 0;
    const float cropPerGazeX =
        static_cast<float>(artwork.scleraWidth - kScreenSize) / kCoordinateMax;
    const float cropPerGazeY =
        static_cast<float>(artwork.scleraHeight - kScreenSize) / kCoordinateMax;
    // The drift is gated by its episode envelope, so between episodes the eye
    // holds perfectly still; the persistent offset is the convergence error;
    // the microsaccade impulses are the small transit texture.
    renderX = std::clamp(
        renderX + static_cast<int>((vergeOffsetX_[vergeEye] +
                                    vergeDriftX_[vergeEye] * vergeDriftEnv_[vergeEye] +
                                    vergeMicroX_[vergeEye]) * cropPerGazeX),
        0, artwork.scleraWidth - kScreenSize);
    renderY = std::clamp(
        renderY + static_cast<int>((vergeOffsetY_[vergeEye] +
                                    vergeDriftY_[vergeEye] * vergeDriftEnv_[vergeEye] +
                                    vergeMicroY_[vergeEye]) * cropPerGazeY),
        0, artwork.scleraHeight - kScreenSize);

    // ── Shared eyelid tracking baseline ───────────────────────────────────
    if (!trackingCurrentFrame_ || lastTrackingUpdateUs_ != nowUs) {
        trackingCurrentFrame_ = true;
        lastTrackingUpdateUs_ = nowUs;
        if (settings.tracking) {
            const std::uint8_t* upper =
                settings.symmetricalEyelid ? artwork.upperSymmetric
                                           : artwork.upperAsymmetric;
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

    // ── Closure -> thresholds ─────────────────────────────────────────────
    const float closure = computeClosure(eyeIndex, nowUs);

    // BOTH lids close by raising their threshold. The renderer treats a pixel
    // as covered when `lidMap[y][x] <= threshold`, so a higher threshold means
    // MORE lid. Driving the lower threshold down instead (as this used to)
    // retracts the lower lid the moment any closure is present, which leaves a
    // hard horizontal edge along the bottom of the eye and never seals.
    //
    // This is why dozing looked wrong rather than merely sleepy: dozing holds a
    // constant elevated closure, so the retracted lower lid was on screen the
    // whole time. Normal mode hid it because blinks are ~150ms out of seconds.
    const int upperThreshold = std::clamp(
        static_cast<int>(trackingUpper_ + (254 - trackingUpper_) * closure), 0, 255);
    const int lowerThreshold = std::clamp(
        static_cast<int>(trackingLower_ + (254 - trackingLower_) * closure), 0, 255);

    // ── Render ────────────────────────────────────────────────────────────
    RenderInput input;
    input.gazeX = renderX;
    input.gazeY = renderY;
    input.irisScale = combineIris(autonomousIris, artwork);
    input.upperThreshold = upperThreshold;
    input.lowerThreshold = lowerThreshold;
    input.lidFade = static_cast<int>(closure * 255.0F);
    input.symmetricalEyelid = settings.symmetricalEyelid;

    // Pupil geometry: an explicitly requested pupil type wins; otherwise the
    // asset's own baked geometry is used.
    if (pupilTypeExplicit_) {
        input.pupilShape = pupilType_ == PupilType::Round
                               ? 0
                               : (pupilType_ == PupilType::VerticalSlit ? 1 : 2);
        input.pupilRatio = profile_.pupilSlitRatio;
    } else {
        input.pupilShape = artwork.pupilShape;
        input.pupilRatio = static_cast<float>(artwork.slitRatio);
    }

    // Eye calibration: use the artwork's baked calibration data
    input.calibration = artwork.pupilGeometry;

    // NEW: Compute calibrated pupil radius from the ratio model
    // This is the pupil boundary that will be enforced by the renderer.
    // The renderer uses the polar sampling model, but the cutoff radius
    // must now come from calibrated geometry rather than raw irisScale.
    const float dilation = environmentalTarget_;
    input.pupilRadius = pupilRadiusFromCalibration(dilation, artwork.pupilGeometry);

    EyeFrame frame;
    frame.render = renderer.render(input);
    frame.gazeX = eyeX;
    frame.gazeY = eyeY;
    frame.renderX = renderX;
    frame.renderY = renderY;
    frame.irisScale = input.irisScale;
    frame.upperThreshold = upperThreshold;
    frame.lowerThreshold = lowerThreshold;
    frame.lidClosure = closure;
    frame.blinkState = (mode_ == AnimationMode::Dozing) ? 0 : blink_.state;
    frame.environmentalPupil = environmentalTarget_;
    frame.currentPupil = pupilResponse_.current();
    frame.agitation = profile_.agitation;
    return frame;
}

}  // namespace uncanny
