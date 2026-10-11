#include "ExpressionProfile.h"

#include <algorithm>

namespace uncanny {
namespace {

struct ModeProfile {
    ExpressionProfile profile;
};

// ── Base profiles, one per first-class mode (TODO A1/A3) ────────────────────
//
// Normal: the reference behaviour. Free gaze wander across the whole legal
// range, full blink rate, no resting lid droop.
//
// Dozing: hands the eyelid channel to DrowsinessModel, which owns droop,
// blinks and micro-sleeps. The profile therefore only constrains the residual
// autonomous behaviour that survives dozing; it does not re-implement the
// sleep model. Dozing gaze is continuous drift, so its hold interval is the
// slowest of the four.
//
// Suspicious: a guarded stare. The eye is free to look anywhere, but it
// changes gaze position rarely ("minimal movement" = how often, never how
// far). Blinks are reduced but never eliminated, lids sit slightly narrowed.
//
// Angry: a hard fixated stare. Same rule: full-range gaze, rare changes.
ExpressionProfile normalProfile() {
    ExpressionProfile profile;
    profile.irisScaleBase = 420;
    profile.irisScaleRange = 300;
    profile.irisScaleMin = 120;
    profile.irisScaleMax = 720;
    // Gaze amplitude is unrestricted -- the eye explores the whole legal
    // range. "Minimal movement" is expressed ONLY as how often the gaze
    // position changes, never as a bound on where the eye may look.
    profile.gazeHoldMinUs = 400'000;
    profile.gazeHoldMaxUs = 3'000'000;
    profile.fixationStrength = 0.0F;
    profile.gazeSpeedScale = 0.7F;
    profile.lidDroop = 0.0F;
    profile.lidAsymmetry = 0.0F;
    profile.blinkIntervalMin = 3.0F;
    profile.blinkIntervalMax = 8.0F;
    profile.blinkDurationMin = 0.07F;
    profile.blinkDurationMax = 0.14F;
    profile.blinkSuppression = 0.0F;
    profile.agitation = 0.0F;
    return profile;
}

ExpressionProfile dozingProfile() {
    ExpressionProfile profile = normalProfile();
    // DrowsinessModel owns the lids, so the autonomous channels are damped to
    // keep the two systems from fighting over the same pixels.
    profile.gazeHoldMinUs = 1'500'000;
    profile.gazeHoldMaxUs = 4'000'000;
    profile.gazeSpeedScale = 0.5F;
    profile.lidDroop = 0.0F;
    profile.blinkSuppression = 1.0F;  // DrowsinessModel schedules all blinks
    // Inherited from the original dozing mode verbatim: `420 - 180 *
    // drowsiness`. NOTE the direction -- this NARROWS the pupil as drowsiness
    // builds (lower scale = more constricted), which is the opposite of the
    // "a drowsy eye dilates" phrasing the project docs have used. The numbers
    // are preserved from the original on purpose; the direction question is
    // tracked separately and is not something this profile silently changes.
    profile.irisScaleBase = 420;
    profile.irisScaleRange = 180;
    return profile;
}

ExpressionProfile suspiciousProfile() {
    ExpressionProfile profile = normalProfile();
    // Suspicious: a guarded stare. The gaze may go anywhere, but it changes
    // position rarely -- this is where "minimal movement" lives.
    profile.gazeHoldMinUs = 2'000'000;
    profile.gazeHoldMaxUs = 5'000'000;
    profile.fixationStrength = 0.7F;
    profile.gazeSpeedScale = 0.30F;
    profile.lidDroop = 0.18F;
    profile.lidAsymmetry = 0.04F;
    profile.blinkIntervalMin = 5.0F;
    profile.blinkIntervalMax = 11.0F;
    profile.blinkSuppression = 0.45F;
    profile.agitation = 0.35F;
    // Arousal constricts the pupil a little. irisScale runs opposite to pupil
    // openness, so a guardedly narrowed eye is a LOWER base than normal's 420.
    // The dilation span (irisScaleRange) has no driver outside dozing, so this
    // mode holds its base rather than wandering around it.
    profile.irisScaleBase = 400;
    profile.irisSweepMin = 3.0F;
    profile.irisSweepMax = 7.0F;
    return profile;
}

ExpressionProfile angryProfile() {
    ExpressionProfile profile = normalProfile();
    // Angry: a hard fixated stare. Full-range gaze, the rarest changes.
    profile.gazeHoldMinUs = 2'500'000;
    profile.gazeHoldMaxUs = 6'000'000;
    profile.fixationStrength = 1.0F;
    profile.gazeSpeedScale = 0.10F;
    profile.lidDroop = 0.34F;
    profile.lidAsymmetry = 0.03F;
    profile.blinkIntervalMin = 7.0F;
    profile.blinkIntervalMax = 14.0F;
    profile.blinkSuppression = 0.8F;
    profile.agitation = 0.85F;
    // Arousal constricts the pupil hard: a much lower base, and like
    // suspicious the stare has no continuous dilation driver, so the base is
    // simply held.
    profile.irisScaleBase = 330;
    profile.irisSweepMin = 4.0F;
    profile.irisSweepMax = 9.0F;
    return profile;
}

ModeProfile profileFor(AnimationMode mode) {
    switch (mode) {
        case AnimationMode::Dozing:
            return {dozingProfile()};
        case AnimationMode::Suspicious:
            return {suspiciousProfile()};
        case AnimationMode::Angry:
            return {angryProfile()};
        case AnimationMode::Normal:
        default:
            return {normalProfile()};
    }
}

float realismScale(Realism level) {
    switch (level) {
        case Realism::Stylized:
            return 0.6F;
        case Realism::Hyperreal:
            return 1.5F;
        case Realism::Realistic:
        default:
            return 1.0F;
    }
}

bool irisTypeAllowsSlit(IrisType type) {
    return type == IrisType::Reptilian || type == IrisType::Goat;
}

}  // namespace

std::string_view modeName(AnimationMode mode) {
    switch (mode) {
        case AnimationMode::Dozing:
            return "dozing";
        case AnimationMode::Suspicious:
            return "suspicious";
        case AnimationMode::Angry:
            return "angry";
        case AnimationMode::Normal:
        default:
            return "normal";
    }
}

bool parseMode(std::string_view text, AnimationMode& mode) {
    if (text == "normal") {
        mode = AnimationMode::Normal;
        return true;
    }
    if (text == "dozing") {
        mode = AnimationMode::Dozing;
        return true;
    }
    if (text == "suspicious") {
        mode = AnimationMode::Suspicious;
        return true;
    }
    if (text == "angry") {
        mode = AnimationMode::Angry;
        return true;
    }
    return false;
}

std::string_view irisTypeName(IrisType type) {
    switch (type) {
        case IrisType::Feline:
            return "feline";
        case IrisType::Reptilian:
            return "reptilian";
        case IrisType::Goat:
            return "goat";
        case IrisType::Mammalian:
        default:
            return "mammalian";
    }
}

bool parseIrisType(std::string_view text, IrisType& type) {
    if (text == "mammalian") {
        type = IrisType::Mammalian;
        return true;
    }
    if (text == "feline") {
        type = IrisType::Feline;
        return true;
    }
    if (text == "reptilian") {
        type = IrisType::Reptilian;
        return true;
    }
    if (text == "goat") {
        type = IrisType::Goat;
        return true;
    }
    return false;
}

std::string_view pupilTypeName(PupilType type) {
    switch (type) {
        case PupilType::VerticalSlit:
            return "slit";
        case PupilType::HorizontalSlit:
            return "horizontal-slit";
        case PupilType::Round:
        default:
            return "round";
    }
}

bool parsePupilType(std::string_view text, PupilType& type) {
    if (text == "round") {
        type = PupilType::Round;
        return true;
    }
    if (text == "slit" || text == "vertical-slit") {
        type = PupilType::VerticalSlit;
        return true;
    }
    if (text == "horizontal-slit") {
        type = PupilType::HorizontalSlit;
        return true;
    }
    return false;
}

std::string_view expressionTypeName(ExpressionType type) {
    switch (type) {
        case ExpressionType::Drowsy:
            return "drowsy";
        case ExpressionType::Suspicious:
            return "suspicious";
        case ExpressionType::Angry:
            return "angry";
        case ExpressionType::Neutral:
        default:
            return "neutral";
    }
}

bool parseExpressionType(std::string_view text, ExpressionType& type) {
    if (text == "neutral") {
        type = ExpressionType::Neutral;
        return true;
    }
    if (text == "drowsy") {
        type = ExpressionType::Drowsy;
        return true;
    }
    if (text == "suspicious") {
        type = ExpressionType::Suspicious;
        return true;
    }
    if (text == "angry") {
        type = ExpressionType::Angry;
        return true;
    }
    return false;
}

std::string_view realismName(Realism level) {
    switch (level) {
        case Realism::Stylized:
            return "stylized";
        case Realism::Hyperreal:
            return "hyperreal";
        case Realism::Realistic:
        default:
            return "realistic";
    }
}

bool parseRealism(std::string_view text, Realism& level) {
    if (text == "stylized") {
        level = Realism::Stylized;
        return true;
    }
    if (text == "realistic") {
        level = Realism::Realistic;
        return true;
    }
    if (text == "hyperreal") {
        level = Realism::Hyperreal;
        return true;
    }
    return false;
}

ExpressionProfile buildProfile(AnimationMode mode, IrisType irisType,
                               PupilType pupilType, Realism realism) {
    ExpressionProfile profile = profileFor(mode).profile;

    // ── Iris classification: dilation envelope and slit eligibility ─────────
    //
    // The iris *texture* is baked per asset; what varies at runtime is the
    // range the iris is allowed to travel and whether a slit is anatomically
    // permitted at all. Clamping irisScaleMin/Max is therefore the whole of
    // "iris type" at runtime -- no renderer change is required.
    switch (irisType) {
        case IrisType::Feline:
            profile.irisScaleMin = 100;
            profile.irisScaleMax = 820;
            profile.irisScaleBase = std::min(profile.irisScaleBase, 520);
            profile.irisScaleRange = std::max(profile.irisScaleRange, 340);
            break;
        case IrisType::Reptilian:
            // Near-fixed pupil: almost no dilation travel.
            profile.irisScaleMin = 400;
            profile.irisScaleMax = 560;
            profile.irisScaleBase = 460;
            profile.irisScaleRange = 60;
            break;
        case IrisType::Goat:
            profile.irisScaleMin = 160;
            profile.irisScaleMax = 680;
            profile.irisScaleBase = std::min(profile.irisScaleBase, 480);
            break;
        case IrisType::Mammalian:
        default:
            break;
    }
    if (profile.irisScaleRange > profile.irisScaleMax - profile.irisScaleMin) {
        profile.irisScaleRange = profile.irisScaleMax - profile.irisScaleMin;
    }

    // ── Pupil geometry override ────────────────────────────────────────────
    //
    // An explicit pupil type wins over the iris type's natural default, so an
    // operator can force a slit onto any design. The renderer receives the
    // resulting geometry through RenderInput and never re-derives it.
    if (pupilType == PupilType::Round) {
        if (!irisTypeAllowsSlit(irisType)) {
            profile.pupilSlitRatio = 0.0F;
        } else {
            profile.pupilSlitRatio = irisType == IrisType::Reptilian ? 0.20F : 0.16F;
        }
    } else {
        profile.pupilSlitRatio = 0.18F;
    }

    // ── Realism: amplitude scaling only, never new branches ───────────────
    const float scale = realismScale(realism);
    profile.lidAsymmetry *= scale;
    profile.agitation = std::min(1.0F, profile.agitation * scale);

    profile.lidDroop = std::min(1.0F, profile.lidDroop * (0.85F + 0.15F * scale));

    return profile;
}

}  // namespace uncanny
