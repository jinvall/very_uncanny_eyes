#pragma once

#include "AlertnessModel.h"
#include "DrowsinessModel.h"
#include "ExpressionProfile.h"
#include "PupilResponseModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uncanny {

constexpr std::size_t kEyeSize = 128;
constexpr std::size_t kEyeCount = 2;

// ── Eye calibration data ─────────────────────────────────────────────────
struct EyeCalibration {
    float limbusDiameter = 100.0F;  // eye-specific diameter in normalized canvas units (200x200)
    float pupilRatioMin = 0.12F;   // Rmin for D = 0.0
    float pupilRatioMax = 0.42F;   // Rmax for D = 1.0
    float pupilCenterX = 100.0F;
    float pupilCenterY = 100.0F;
};

// ── Artwork (TODO A5: generated tables are build artifacts) ────────────────
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
    // Default non-round pupil baked into this asset. 0 = round, 1 = vertical
    // slit, 2 = horizontal slit; slitRatio is the short semi-axis as a
    // fraction of the long one. Used only when no explicit pupil type is
    // requested on the command line -- the profile normally overrides it, and
    // the override travels through RenderInput rather than being re-derived by
    // the renderer.
    int pupilShape = 0;
    double slitRatio = 0.0;
    // Eye calibration data for normalized ratio model
    EyeCalibration pupilGeometry;
};

struct EyeRender {
    std::array<std::uint8_t, kEyeSize * kEyeSize * 4> rgba{};
};

// ── Render input contract (TODO A2) ─────────────────────────────────────────
// Everything the renderer is allowed to know. The renderer is a pure function
// of this struct plus the artwork: it holds no state, reads no animator, and
// never branches on the runtime mode. That is what makes a frame reproducible
// from its inputs alone, and what lets new eye types be added by changing a
// profile rather than by teaching the renderer about them.
struct RenderInput {
    // Sclera crop origin, in source-artwork pixels.
    int gazeX = 512;
    int gazeY = 512;
    // Iris scale, 0..1023. Higher = more dilated (wider pupil); lower = more
    // constricted. irisScaleMin on the artwork is the most-constricted end of
    // the range and irisScaleMax the most-dilated end.
    int irisScale = 420;
    // Eyelid coverage thresholds, 0..255, sampled against the generated
    // eyelid maps.
    int upperThreshold = 128;
    int lowerThreshold = 0;
    // 0..255 fade applied to the outer sclera band. Used by the doze artwork
    // variant so a closing eye does not leave bright caruncle corners.
    int lidFade = 0;
    // Which generated eyelid map to sample. The asymmetric map is the correct
    // anatomy for the real eye; the symmetric one exists for designs that were
    // authored without a mirrored pair.
    bool symmetricalEyelid = false;
    // Non-round pupil geometry. 0 = round, 1 = vertical slit, 2 = horizontal
    // slit. pupilRatio is the short semi-axis as a fraction of the long one.
    int pupilShape = 0;
    float pupilRatio = 0.0F;
    // Eye calibration data for normalized ratio model
    EyeCalibration calibration;
    // Computed pupil radius from calibrated ratio model (derived from calibration)
    float pupilRadius = 0.0F;
};

// Per-eye animation state. This is animator-internal and is deliberately NOT
// passed to the renderer: the animator's bookkeeping is not render input.
// The shared gaze target and its interpolation state.
//
// `currentX/currentY` is the resolved position for the frame, written exactly
// once per frame by the state machine. Per-eye resolution is a pure read of
// those fields, so the two eyes cannot observe a gaze state one frame apart
// from each other -- which is exactly what happened when the easing and the
// target picking both ran inside the per-eye update.
struct GazeMotion {
    bool inMotion = false;
    int baseX = 512;     // where the current movement started
    int baseY = 512;
    int newX = 512;      // where it is heading
    int newY = 512;
    int currentX = 512;  // resolved position for this frame
    int currentY = 512;
    std::uint64_t moveStartTimeUs = 0;
    std::int64_t moveDurationUs = 0;
    // True while the current target came from the autonomous wander (as
    // opposed to an explicit setGaze). A fatigued dozing eye lands autonomous
    // targets with a sloppy offset; an explicitly requested position must
    // still be hit exactly.
    bool autonomous = false;
};

struct BlinkState {
    int state = 0;  // 0 none, 1 closing, 2 opening
    std::uint64_t startTimeUs = 0;
    std::uint64_t durationUs = 0;
};

// A rendered frame plus the values the smoke test and the operator tooling
// need to inspect.
struct EyeFrame {
    EyeRender render;
    int gazeX = 0;
    int gazeY = 0;
    int renderX = 0;
    int renderY = 0;
    int irisScale = 0;
    int upperThreshold = 0;
    int lowerThreshold = 0;
    int blinkState = 0;
    float lidClosure = 0.0F;          // 0 open, 1 closed
    float environmentalPupil = 0.0F;  // raw light target, 0..1
    float currentPupil = 0.0F;        // after the physiological response
    float agitation = 0.0F;
};

// Normalized pupil geometry for one dilation state (TODO B5).
//
// This is the single source of truth for "how big is the pupil and where does
// it end", shared by the renderer and by the regression suite so the bound that
// is asserted in tests is the bound the renderer actually uses, not a copy of
// it that can drift.
//
// The invariant it exists to guarantee: the pupil is always contained within
// the iris disc. For a round pupil that is structural -- the polar distance
// field is defined only inside the disc. For a slit it is not, because a slit
// is an ellipse in its own right, so the semi-axes are derived from the same
// normalized radius the round pupil uses and are asserted never to exceed it.
struct PupilGeometry {
    // 0..128. The iris is drawn where polar distance < irisThreshold, so the
    // pupil occupies the rest of the disc.
    int irisThreshold = 0;
    // Pupil radius in iris-artwork pixels.
    double radius = 0.0;
    // Slit semi-axes. Equal to `radius` for a round pupil.
    double semiMinor = 0.0;
    double semiMajor = 0.0;
    // Radius of the containing iris disc in iris-artwork pixels.
    double irisRadius = 0.0;
};

// `pupilShape`: 0 = round, 1 = vertical slit, 2 = horizontal slit.
// `pupilRatio` is the short semi-axis as a fraction of the long one.
PupilGeometry pupilGeometry(int irisScale, int pupilShape, float pupilRatio,
                            int irisHeight, const EyeCalibration& calibration = EyeCalibration{});

// Convert normalized dilation (0..1) to pupil-to-limbus ratio using calibration
float dilationToPupilRatio(float dilation, const EyeCalibration& calibration);

// Calculate pupil radius from calibrated ratio model
float pupilRadiusFromCalibration(float dilation, const EyeCalibration& calibration);

struct RenderSettings {
    bool tracking = true;
    bool symmetricalEyelid = false;
    int irisMin = 120;
    int irisMax = 720;
};

struct AnimationSettings {
    bool autonomousGaze = true;
    bool autonomousBlink = true;
    // Opt-in: with no light sensor there is no reason for the pupil to move on
    // its own, so the default is a still pupil that only the mode, the LDR or
    // an explicit setIrisScale() can move.
    bool autonomousIris = false;
    std::uint64_t gazeSeed = 1;
    float environmentalIrisWeight = 0.0F;
    float constrictionTime = 0.35F;
    float dilationTime = 0.80F;
};

// ── Renderer ───────────────────────────────────────────────────────────────
class EyeRenderer {
public:
    explicit EyeRenderer(const EyeArtwork& artwork);

    // Pure: identical RenderInput + identical artwork => identical EyeRender.
    EyeRender render(const RenderInput& input) const;

private:
    const EyeArtwork& artwork_;
};

// ── Animator ───────────────────────────────────────────────────────────────
class EyeAnimator {
public:
    explicit EyeAnimator(AnimationSettings settings);

    // Eye classification, applied on top of the mode profile (TODO B4).
    // These constrain the dilation envelope and the motion amplitudes only;
    // they never override an asset's own baked pupil geometry.
    void setEyeTypes(IrisType irisType, Realism realism);
    // Forcing a pupil geometry does override the asset's baked shape, so it is
    // a separate, explicitly-named call.
    void setPupilType(PupilType pupilType);

    EyeFrame update(std::size_t eyeIndex, std::uint64_t nowUs,
                    const EyeArtwork& artwork, const EyeRenderer& renderer,
                    const RenderSettings& settings);

    void setMode(AnimationMode mode, std::uint64_t nowUs);
    void blink(std::uint64_t nowUs);
    void setGaze(std::size_t eyeIndex, int x, int y);
    void setIrisScale(int scale);
    void setIrisScaleOverride(bool enabled) { irisScaleOverride_ = enabled; }
    bool irisScaleOverride() const { return irisScaleOverride_; }

    // Light target, 0..1 (1 = fully dilated / dark). `active` false relaxes
    // the pupil to its resting size.
    void setEnvironmentalPupil(float pupil, std::uint64_t nowUs);
    void setEnvironmentalActive(bool active) { environmentalActive_ = active; }

    int irisScale() const { return irisScale_; }
    float currentPupil() const { return pupilResponse_.current(); }
    float targetPupil() const { return pupilResponse_.target(); }
    AnimationMode mode() const { return mode_; }
    const ExpressionProfile& profile() const { return profile_; }
    const DrowsinessModel& drowsiness() const { return drowsiness_; }
    DrowsinessModel& drowsiness() { return drowsiness_; }
    const AlertnessModel& alertness() const { return alertness_; }
    void setDrowsinessConfig(const DrowsinessConfig& config) { drowsiness_.setConfig(config); }

    // Runtime tuning (TODO C3). Values are validated by clamping, so the TUI
    // and CLI can drive them without a rebuild.
    void setExpressionIntensity(float intensity);
    void setLdrSensitivity(float sensitivity);
    float expressionIntensity() const { return expressionIntensity_; }
    float ldrSensitivity() const { return ldrSensitivity_; }

private:
    std::uint32_t random();
    std::uint32_t random(std::uint32_t low, std::uint32_t high);
    float randomUnit();
    void rebuildProfile();
    void beginIrisMotion(std::uint64_t nowUs);
    std::uint64_t nextBlinkDelayUs();
    void updateBlink(std::uint64_t nowUs);
    void maybeScheduleBlink(std::uint64_t nowUs);
    void advanceGaze(std::uint64_t nowUs);
    // Dozing nod-off choreography: roll the gaze while the lids are shut, snap
    // back to the remembered fixation on waking, then run the lateral glance.
    // Runs once per frame, inside the shared block, and overrides the saccade
    // machine while a nod-off sequence is active.
    void applyDozingNodOffGaze(std::uint64_t nowUs);
    // How long the eye holds the current gaze position before picking a new
    // one. This is the "minimal movement" control: how OFTEN the gaze changes,
    // never how far the eye travels.
    std::int64_t gazeHoldUs();
    // Dozing fatigue: slow saccades (duration multiplier), rare gaze changes
    // (hold multiplier) and sloppy landings, all scaled by drowsiness 0..1.
    float dozingSaccadeSlowdown() const;
    float dozingHoldStretch() const;
    float dozingLandingError() const;
    // Per-eye vergence appearance: fixation drift and microsaccade impulses run
    // on each eye's own phase, so a fatigued pair reads as losing alignment
    // (momentary divergence) without breaking the shared-gaze mirror.
    void updateVergence(std::size_t eyeIndex, std::uint64_t nowUs);
    void resolveGaze(int& outGazeX, int& outGazeY) const;
    float computeClosure(std::size_t eyeIndex, std::uint64_t nowUs);
    int combineIris(int autonomousScale, const EyeArtwork& artwork) const;
    int restingIrisScale(const EyeArtwork& artwork) const;

    AnimationSettings settings_;
    AnimationMode mode_ = AnimationMode::Normal;
    IrisType irisType_ = IrisType::Mammalian;
    PupilType pupilType_ = PupilType::Round;
    Realism realism_ = Realism::Realistic;
    bool pupilTypeExplicit_ = false;
    bool irisScaleOverride_ = false;
    ExpressionProfile profile_;

    GazeMotion gaze_;
    BlinkState blink_;

    // ── Dozing fatigue: per-eye vergence appearance ────────────────────────
    // One entry per eye. Each eye gets its own slow fixation drift walk and its
    // own irregular microsaccade impulses; because the two walks are
    // independent, the pair intermittently points at different places -- the
    // classic fatigued-vergence read -- while the shared saccade trajectory
    // keeps the display mirror intact.
    float vergeDriftX_[kEyeCount]{};     // slow mean-reverting walk, gaze units
    float vergeDriftY_[kEyeCount]{};
    float vergeOffsetX_[kEyeCount]{};    // persistent convergence error, gaze units
    float vergeOffsetY_[kEyeCount]{};
    float vergeDriftEnv_[kEyeCount]{};   // episode envelope, 0..1
    std::int64_t vergeDriftEpisodeUs_[kEyeCount]{};  // remaining episode time
    std::int64_t vergeDriftEpisodeLenUs_[kEyeCount]{};
    std::int64_t vergeDriftRestUs_[kEyeCount]{};     // stillness between episodes
    std::int64_t vergeDriftAgeUs_[kEyeCount]{};
    float vergeMicroX_[kEyeCount]{};     // fast decaying corrective impulses
    float vergeMicroY_[kEyeCount]{};
    std::int64_t vergeMicroTimerUs_[kEyeCount]{};  // time to the next impulse
    std::uint64_t vergeLastUs_[kEyeCount]{};

    // ── Dozing nod-off gaze choreography ──────────────────────────────────
    bool nodOffActive_ = false;        // lids shut for a micro-sleep right now
    bool nodOffSequenceActive_ = false; // any nod-off phase is running
    bool nodOffReturning_ = false;     // snapping back to the remembered fixation
    int nodOffGazeX_ = 512;            // fixation held when the nod-off began
    int nodOffGazeY_ = 512;
    int nodOffReturnFromX_ = 512;      // where the gaze had rolled to on waking
    int nodOffReturnFromY_ = 512;
    float nodOffRollX_ = 0.0F;         // slow roll while asleep, gaze units
    float nodOffRollY_ = 0.0F;
    float nodOffRollProgressX_ = 0.0F; // how far the roll has come
    float nodOffRollProgressY_ = 0.0F;
    std::uint64_t nodOffReturnStartUs_ = 0;
    std::uint64_t nodOffLastUs_ = 0;
    int trackingUpper_ = 128;
    int trackingLower_ = 0;
    std::uint64_t lastTrackingUpdateUs_ = 0;
    bool trackingCurrentFrame_ = false;

    std::uint64_t lastFrameUs_ = 0;
    std::uint64_t nextBlinkUs_ = 0;

    int irisScale_ = 420;
    int oldIris_ = 420;
    int newIris_ = 420;
    std::uint64_t irisStartUs_ = 0;
    std::int64_t irisDurationUs_ = 0;

    PupilResponseModel pupilResponse_;
    bool environmentalActive_ = false;
    float environmentalTarget_ = 0.0F;

    float expressionIntensity_ = 1.0F;
    float ldrSensitivity_ = 1.0F;

    std::uint32_t randomState_;

    DrowsinessModel drowsiness_;
    AlertnessModel alertness_;
};

}  // namespace uncanny
