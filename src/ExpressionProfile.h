#pragma once

#include <cstdint>
#include <string_view>

namespace uncanny {

// ── Eye behaviour classification ────────────────────────────────────────────
// These are *behavioural* classifiers, not artwork selectors. The fine iris
// texture is baked into each generated asset; what the runtime varies is how
// the iris and pupil *behave* under dilation.
enum class IrisType {
    Mammalian,   // round pupil, wide dilation range
    Feline,      // near-round pupil, very wide dilation range, vertical slit when constricted
    Reptilian,   // fixed narrow slit, minimal dilation
    Goat,        // horizontal pupil, moderate dilation
};

enum class PupilType {
    Round,
    VerticalSlit,
    HorizontalSlit,
};

enum class ExpressionType {
    Neutral,
    Drowsy,
    Suspicious,
    Angry,
};

// Scales how much independent life the eye shows. This multiplies motion
// amplitudes rather than selecting code paths, so raising realism never adds a
// new renderer branch.
enum class Realism {
    Stylized,   // calm, readable, minimal jitter
    Realistic,  // default
    Hyperreal,  // pronounced micro-motion and agitation
};

// ── Expression profile ─────────────────────────────────────────────────────
// A single value object describing everything the animator needs to know about
// how an eye behaves in a given mode. The renderer never reads a profile; it
// only consumes the derived RenderInput. That is what keeps mode logic out of
// the renderer (TODO A2/A3).
struct ExpressionProfile {
    // Iris / pupil behaviour. irisScale runs OPPOSITE to pupil openness: a
    // lower scale is a narrower (more constricted) pupil, a higher scale a
    // wider (more dilated) one. irisScaleMin is therefore the most-constricted
    // end of the range and irisScaleMax the most-dilated end.
    int irisScaleBase = 420;      // resting iris scale, 0..1023
    int irisScaleRange = 300;     // dilation span driven by the mode's sleep channel (dozing)
    int irisScaleMin = 120;       // most-constricted clamp
    int irisScaleMax = 720;       // most-dilated clamp
    float pupilSlitRatio = 0.0F;  // 0 = round; otherwise short/long semi-axis ratio

    // Gaze pattern.
    // How long the eye holds a gaze position before it picks a new one, in
    // microseconds. This is the ONLY "minimal movement" axis: how often the
    // eyes change gaze position. Gaze amplitude is deliberately NOT
    // restricted -- the eye looks wherever the animation puts it, bounded only
    // by the artwork bounds (the legal-position clamp).
    std::int64_t gazeHoldMinUs = 400'000;
    std::int64_t gazeHoldMaxUs = 3'000'000;
    float gazeSpeedScale = 1.0F;    // how fast the eye travels once it moves
    float fixationStrength = 0.0F;  // 0 = free wander, 1 = locked stare

    // Eyelid posture.
    float lidDroop = 0.0F;       // resting closure added to the tracked lid, 0..1
    float lidAsymmetry = 0.0F;   // left/right lid difference, 0..1

    // Blink rhythm.
    float blinkIntervalMin = 3.0F;   // seconds
    float blinkIntervalMax = 8.0F;   // seconds
    float blinkDurationMin = 0.07F;  // seconds
    float blinkDurationMax = 0.14F;  // seconds
    float blinkSuppression = 0.0F;   // 0 = full blink rate, 1 = no blinking

    // Agitation / alertness blend. Drives pupil constriction, lid tension and
    // the frequency of involuntary glances.
    float agitation = 0.0F;

    // Ambient iris travel (autonomous iris breathing), seconds per sweep.
    float irisSweepMin = 2.0F;
    float irisSweepMax = 6.0F;
};

// ── Mode registry (TODO A1) ────────────────────────────────────────────────
// The four first-class runtime modes. Every mode resolves to exactly one
// profile; nothing else in the codebase branches on the mode directly.
enum class AnimationMode {
    Normal,
    Dozing,
    Suspicious,
    Angry,
};

// Registry helpers. These are the only sanctioned way to convert between the
// mode and its textual form, so `--help`, the TUI and the smoke test can never
// disagree about what modes exist.
std::string_view modeName(AnimationMode mode);
bool parseMode(std::string_view text, AnimationMode& mode);

std::string_view irisTypeName(IrisType type);
bool parseIrisType(std::string_view text, IrisType& type);
std::string_view pupilTypeName(PupilType type);
bool parsePupilType(std::string_view text, PupilType& type);
std::string_view expressionTypeName(ExpressionType type);
bool parseExpressionType(std::string_view text, ExpressionType& type);
std::string_view realismName(Realism level);
bool parseRealism(std::string_view text, Realism& level);

// Builds the profile for a mode, then layers the eye-classification and
// realism adjustments over it. This is the single composition point: callers
// pass the classification, never a hand-built profile.
ExpressionProfile buildProfile(AnimationMode mode, IrisType irisType,
                               PupilType pupilType, Realism realism);

}  // namespace uncanny
