#include "LDRSerialInput.h"
#include "DrowsinessModel.h"
#include "artwork.h"

#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kCoordinateMax = 1023;
constexpr float kEyeWidthFraction = 0.36F;
constexpr float kEyeHeightFraction = 0.72F;
constexpr float kEyeGapFraction = 0.14F;
constexpr float kMinimumGapPixels = 16.0F;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
constexpr Uint32 kTextureFormat = SDL_PIXELFORMAT_RGBA8888;
#else
constexpr Uint32 kTextureFormat = SDL_PIXELFORMAT_ABGR8888;
#endif

struct AppOptions {
    bool fullscreen = false;
    bool borderless = false;
    // TODO C1: the remote display is a fixed 16:9 projection, so a dedicated
    // mode that is always borderless and always 16:9 removes the chance of
    // streaming a window that has drifted off-ratio or picked up decorations.
    bool streamMode = false;
    bool autonomousIris = false;
    bool smokeTest = false;
    int width = 1920;
    int height = 1080;
    std::string design = "default";
    uncanny::AnimationMode mode = uncanny::AnimationMode::Normal;
    uncanny::IrisType irisType = uncanny::IrisType::Mammalian;
    uncanny::PupilType pupilType = uncanny::PupilType::Round;
    uncanny::Realism realism = uncanny::Realism::Realistic;
    bool pupilTypeExplicit = false;
    int irisScaleOverride = -1;
    bool irisScaleOverrideEnabled = false;
    float expressionIntensity = 1.0F;
    float ldrSensitivity = 1.0F;
    float scale = 1.0F;
    std::array<std::uint8_t, 3> background{0, 0, 0};
    int frameRate = 60;
    bool listDesigns = false;
    // Vocabulary listings, so the operator TUI never has to hardcode a value
    // the build does not accept.
    bool listModes = false;
    bool listIrisTypes = false;
    bool listPupilTypes = false;
    bool listRealism = false;
    bool help = false;
    uncanny::LDRSerialConfig ldr;
    bool drowsinessEnabled = false;
    uncanny::DrowsinessConfig drowsiness;
    std::string faceOverlayPath;  // Path to face overlay PNG with transparent eye holes
};

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parseInteger(const std::string& value, int& result) {
    char* end = nullptr;
    try {
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (*end != '\0' || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        result = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloat(const std::string& value, float& result) {
    try {
        const float parsed = std::stof(value);
        if (!std::isfinite(parsed)) {
            return false;
        }
        result = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, AppOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--fullscreen") {
            options.fullscreen = true;
        } else if (argument == "--borderless") {
            options.borderless = true;
        } else if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--list-designs") {
            options.listDesigns = true;
        } else if (argument == "--list-modes") {
            options.listModes = true;
        } else if (argument == "--list-iris-types") {
            options.listIrisTypes = true;
        } else if (argument == "--list-pupil-types") {
            options.listPupilTypes = true;
        } else if (argument == "--list-realism") {
            options.listRealism = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--design" && i + 1 < argc) {
            options.design = lowerCopy(argv[++i]);
        } else if (argument == "--mode" && i + 1 < argc) {
            const std::string modeName = lowerCopy(argv[++i]);
            if (!uncanny::parseMode(modeName, options.mode)) {
                throw std::runtime_error(
                    "invalid --mode value; use normal, dozing, suspicious or angry");
            }
        } else if (argument == "--iris-type" && i + 1 < argc) {
            const std::string value = lowerCopy(argv[++i]);
            if (!uncanny::parseIrisType(value, options.irisType)) {
                throw std::runtime_error(
                    "invalid --iris-type value; use mammalian, feline, reptilian or goat");
            }
        } else if (argument == "--pupil-type" && i + 1 < argc) {
            const std::string value = lowerCopy(argv[++i]);
            if (!uncanny::parsePupilType(value, options.pupilType)) {
                throw std::runtime_error(
                    "invalid --pupil-type value; use round, slit or horizontal-slit");
            }
            options.pupilTypeExplicit = true;
        } else if ((argument == "--iris-scale" || argument == "--debug-iris-scale") && i + 1 < argc) {
            const std::string value = lowerCopy(argv[++i]);
            int scale = -1;
            if (value == "min") {
                scale = 0;
            } else if (value == "max") {
                scale = 1023;
            } else if (!parseInteger(value, scale) || scale < 0 || scale > 1023) {
                throw std::runtime_error("invalid --iris-scale value; use 0..1023 or min/max");
            }
            options.irisScaleOverride = scale;
            options.irisScaleOverrideEnabled = true;
        } else if (argument == "--realism" && i + 1 < argc) {
            const std::string value = lowerCopy(argv[++i]);
            if (!uncanny::parseRealism(value, options.realism)) {
                throw std::runtime_error(
                    "invalid --realism value; use stylized, realistic or hyperreal");
            }
        } else if (argument == "--expression-intensity" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.expressionIntensity) ||
                options.expressionIntensity < 0.0F || options.expressionIntensity > 2.0F) {
                throw std::runtime_error("invalid --expression-intensity value; use 0..2");
            }
        } else if (argument == "--ldr-sensitivity" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldrSensitivity) ||
                options.ldrSensitivity <= 0.0F || options.ldrSensitivity > 4.0F) {
                throw std::runtime_error("invalid --ldr-sensitivity value; use 0..4 (0 exclusive)");
            }
        } else if (argument == "--width" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.width) || options.width <= 0) {
                throw std::runtime_error("invalid --width value");
            }
        } else if (argument == "--height" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.height) || options.height <= 0) {
                throw std::runtime_error("invalid --height value");
            }
        } else if (argument == "--scale" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.scale) || options.scale <= 0.0F || options.scale > 100.0F) {
                throw std::runtime_error("invalid --scale value");
            }
        } else if (argument == "--fps" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.frameRate) || options.frameRate <= 0 || options.frameRate > 240) {
                throw std::runtime_error("invalid --fps value");
            }
        } else if (argument == "--background" && i + 1 < argc) {
            int red = 0;
            int green = 0;
            int blue = 0;
            const std::string color = argv[++i];
            const auto comma1 = color.find(',');
            const auto comma2 = comma1 == std::string::npos ? std::string::npos : color.find(',', comma1 + 1);
            if (comma1 == std::string::npos || comma2 == std::string::npos ||
                !parseInteger(color.substr(0, comma1), red) ||
                !parseInteger(color.substr(comma1 + 1, comma2 - comma1 - 1), green) ||
                !parseInteger(color.substr(comma2 + 1), blue) ||
                red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {
                throw std::runtime_error("invalid --background value; use R,G,B");
            }
            options.background = {static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                  static_cast<std::uint8_t>(blue)};
        } else if (argument == "--autonomous-iris") {
            // Let the pupil drift on its own with no light sensor attached.
            // Off by default: a self-driven pupil reads as twitching for no
            // reason. The mode still moves the pupil -- dozing dilates as
            // drowsiness builds.
            options.autonomousIris = true;
        } else if (argument == "--stream-mode") {
            // 16:9, borderless, black background, no window manager chrome.
            options.streamMode = true;
            options.borderless = true;
            options.fullscreen = false;
            options.background = {0, 0, 0};
        } else if (argument == "--ldr") {
            options.ldr.enabled = true;
        } else if (argument == "--ldr-port" && i + 1 < argc) {
            options.ldr.port = argv[++i];
        } else if (argument == "--ldr-baud" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.ldr.baud) || options.ldr.baud <= 0) {
                throw std::runtime_error("invalid --ldr-baud value");
            }
        } else if (argument == "--ldr-min" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.ldr.sensorMin)) {
                throw std::runtime_error("invalid --ldr-min value");
            }
        } else if (argument == "--ldr-max" && i + 1 < argc) {
            if (!parseInteger(argv[++i], options.ldr.sensorMax)) {
                throw std::runtime_error("invalid --ldr-max value");
            }
        } else if (argument == "--ldr-invert") {
            options.ldr.invert = true;
        } else if (argument == "--ldr-alpha" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.filterAlpha) || options.ldr.filterAlpha <= 0.0F || options.ldr.filterAlpha > 1.0F) {
                throw std::runtime_error("invalid --ldr-alpha value");
            }
        } else if (argument == "--ldr-min-pupil" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.minPupil) || options.ldr.minPupil < 0.0F || options.ldr.minPupil > 1.0F) {
                throw std::runtime_error("invalid --ldr-min-pupil value");
            }
        } else if (argument == "--ldr-max-pupil" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.maxPupil) || options.ldr.maxPupil < 0.0F || options.ldr.maxPupil > 1.0F) {
                throw std::runtime_error("invalid --ldr-max-pupil value");
            }
        } else if (argument == "--ldr-constriction" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.constrictionTime) || options.ldr.constrictionTime <= 0.0F) {
                throw std::runtime_error("invalid --ldr-constriction value");
            }
        } else if (argument == "--ldr-dilation" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.dilationTime) || options.ldr.dilationTime <= 0.0F) {
                throw std::runtime_error("invalid --ldr-dilation value");
            }
        } else if (argument == "--ldr-timeout" && i + 1 < argc) {
            int timeoutMs = 0;
            if (!parseInteger(argv[++i], timeoutMs) || timeoutMs < 0) {
                throw std::runtime_error("invalid --ldr-timeout value");
            }
            options.ldr.disconnectTimeoutUs = static_cast<std::uint64_t>(timeoutMs) * 1000U;
        } else if (argument == "--ldr-no-reconnect") {
            options.ldr.autoReconnect = false;
        } else if (argument == "--ldr-curve" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.curve) || options.ldr.curve <= 0.0F) {
                throw std::runtime_error("invalid --ldr-curve value");
            }
        } else if (argument == "--ldr-iris-weight" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.ldr.irisWeight) || options.ldr.irisWeight < 0.0F || options.ldr.irisWeight > 1.0F) {
                throw std::runtime_error("invalid --ldr-iris-weight value");
            }
        } else if (argument == "--drowsiness-enable") {
            options.drowsiness.enabled = true;
        } else if (argument == "--drowsiness-baseline" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.drowsiness.baseline) || options.drowsiness.baseline < 0.0F || options.drowsiness.baseline > 1.0F) {
                throw std::runtime_error("invalid --drowsiness-baseline value");
            }
        } else if (argument == "--drowsiness-accum" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.drowsiness.accumulationRate) || options.drowsiness.accumulationRate <= 0.0F) {
                throw std::runtime_error("invalid --drowsiness-accum value");
            }
        } else if (argument == "--drowsiness-recover" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.drowsiness.recoveryRate) || options.drowsiness.recoveryRate <= 0.0F) {
                throw std::runtime_error("invalid --drowsiness-recover value");
            }
        } else if (argument == "--drowsiness-droop" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.drowsiness.eyelidDroopMax) || options.drowsiness.eyelidDroopMax < 0.0F || options.drowsiness.eyelidDroopMax > 1.0F) {
                throw std::runtime_error("invalid --drowsiness-droop value");
            }
        } else if (argument == "--drowsiness-microsleep" && i + 1 < argc) {
            if (!parseFloat(argv[++i], options.drowsiness.microSleepProbMax) || options.drowsiness.microSleepProbMax < 0.0F || options.drowsiness.microSleepProbMax > 1.0F) {
                throw std::runtime_error("invalid --drowsiness-microsleep value");
            }
        } else if (argument == "--face-overlay" && i + 1 < argc) {
            options.faceOverlayPath = argv[++i];
        } else {
            throw std::runtime_error("unknown argument: " + argument);
        }
    }
    if (options.ldr.enabled && options.ldr.port.empty()) {
        throw std::runtime_error("--ldr requires --ldr-port");
    }
    if (options.ldr.minPupil > options.ldr.maxPupil) {
        throw std::runtime_error("--ldr-min-pupil must not exceed --ldr-max-pupil");
    }
    if (options.ldr.sensorMax <= options.ldr.sensorMin) {
        throw std::runtime_error("--ldr-max must be greater than --ldr-min");
    }
    return true;
}

void printUsage(const char* executable) {
    // Build the design list from the registry so it cannot go stale the way a
    // hardcoded string did -- it once advertised 3 of the 6 shipped designs.
    std::string designs;
    for (const uncanny::EyeArtwork* artwork : uncanny::artworkList()) {
        if (!designs.empty()) {
            designs += ", ";
        }
        designs += artwork->name;
    }
    if (designs.empty()) {
        designs = "(none)";
    }

    std::cout << "Usage: " << executable << " [options]\n"
              << "  --fullscreen             Use the active display in fullscreen mode\n"
              << "  --borderless             Windowed mode with no window decorations\n"
              << "  --stream-mode            Lock to 16:9 borderless black output (1080p)\n"
              << "  --autonomous-iris        Let the pupil drift with no light sensor\n"
              << "                          (off by default: no reason for the pupil to move)\n"
              << "  --smoke-test             Render a few frames and exit without opening a window\n"
              << "  --width PIXELS           Window width (default: 1920)\n"
              << "  --height PIXELS          Window height (default: 1080)\n"
              << "  --design NAME            Available designs: " << designs << "\n"
              << "  --mode NAME              Animation mode: normal, dozing, suspicious, angry\n"
              << "                          (default: normal)\n"
              << "  --iris-type NAME         Iris classification: mammalian, feline, reptilian, goat\n"
              << "  --pupil-type NAME        Pupil geometry: round, slit, horizontal-slit\n"
              << "  --iris-scale VALUE       Force pupil dilation to a fixed scale\n"
              << "                          (0..1023 or min/max; clamped to the\n"
              << "                          mode's physiological pupil range)\n"
              << "  --realism NAME           Motion realism: stylized, realistic, hyperreal\n"
              << "  --expression-intensity F Overall expression strength, 0..2 (default: 1.0)\n"
              << "  --ldr-sensitivity F      Light-response sensitivity, 0..4 (default: 1.0)\n"
              << "  --scale FACTOR           Additional eye scale factor (default: 1.0)\n"
              << "  --background R,G,B       Window/fullscreen background (default: 0,0,0)\n"
              << "  --fps RATE               Animation frame rate, 1-240 (default: 60)\n"
              << "  --ldr                    Enable USB serial LDR input\n"
              << "  --ldr-port PATH          Serial device (required with --ldr)\n"
              << "  --ldr-baud RATE          Serial baud rate (default: 115200)\n"
              << "  --ldr-min VALUE          ADC minimum (default: 0)\n"
              << "  --ldr-max VALUE          ADC maximum (default: 4095)\n"
              << "  --ldr-invert             Reverse normalized sensor direction\n"
              << "  --ldr-alpha VALUE        Low-pass alpha, 0 < alpha <= 1 (default: 0.15)\n"
              << "  --ldr-min-pupil VALUE    Bright-light pupil fraction (default: 0.25)\n"
              << "  --ldr-max-pupil VALUE    Dark-light pupil fraction (default: 1.0)\n"
              << "  --ldr-constriction SEC   Bright-response time (default: 0.35)\n"
              << "  --ldr-dilation SEC       Dark-response time (default: 0.80)\n"
              << "  --ldr-timeout MS         Hold-last-state timeout (default: 2000)\n"
              << "  --ldr-no-reconnect       Disable automatic reconnect\n"
              << "  --ldr-curve EXP          Nonlinear light-to-pupil curve (default: 0.45)\n"
              << "  --ldr-iris-weight F      Blend LDR with autonomous iris (default: 0.75)\n"
              << "  --drowsiness-enable       Enable drowsiness behavioral system\n"
              << "  --drowsiness-baseline F   Starting drowsiness 0..1 (default: 0)\n"
              << "  --drowsiness-accum F      Accumulation rate per sec (default: 0.015)\n"
              << "  --drowsiness-recover F    Recovery rate per sec (default: 0.08)\n"
              << "  --drowsiness-droop F      Max eyelid droop 0..1 (default: 0.35)\n"
              << "  --drowsiness-microsleep F Max micro-sleep prob/sec (default: 0.04)\n"
              << "  --face-overlay PATH     PNG face overlay with transparent eye holes\n"
              << "  --list-designs           Print available eye designs and exit\n"
              << "  --list-modes             Print available animation modes and exit\n"
              << "  --list-iris-types        Print available iris types and exit\n"
              << "  --list-pupil-types       Print available pupil geometries and exit\n"
              << "  --list-realism           Print available realism levels and exit\n"
              << "  --help                   Print this help and exit\n"
              << "  D                        Toggle dozing mode at runtime\n";
}

const uncanny::EyeArtwork& findArtwork(const std::string& design) {
    for (const uncanny::EyeArtwork* artwork : uncanny::artworkList()) {
        if (lowerCopy(std::string(artwork->name)) == design) {
            return *artwork;
        }
    }
    throw std::runtime_error("unknown eye design: " + design);
}

void printDesigns() {
    for (const uncanny::EyeArtwork* artwork : uncanny::artworkList()) {
        std::cout << artwork->name << "\n";
    }
}

void printVocabulary(std::initializer_list<std::string_view> values) {
    for (std::string_view value : values) {
        std::cout << value << "\n";
    }
}

std::uint64_t nowMicroseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

namespace {

// Drives an animator across a time range and reports the extremes observed.
// Used instead of asserting a fixed expected value, because the drowsiness and
// alertness models are deliberately aperiodic: they generate fresh parameters
// at every trigger and must not repeat on a recognisable cycle. What is
// testable is that they stay in bounds, stay synchronized across both eyes,
// and actually produce visible eyelid movement.
struct SweepResult {
    float minClosure = 1.0F;
    float maxClosure = 0.0F;
    int minUpper = 255;
    int maxUpper = 0;
    int minLower = 255;
    int maxLower = 0;
    bool outOfBounds = false;
    bool unsynchronized = false;
};

SweepResult sweep(uncanny::EyeAnimator& animator, const uncanny::EyeArtwork& artwork,
                  const uncanny::EyeRenderer& renderer,
                  const uncanny::RenderSettings& settings,
                  uncanny::AnimationMode mode, std::uint64_t startUs,
                  std::uint64_t durationUs) {
    constexpr std::uint64_t kFramePeriod = 16'667;
    // Widest permitted difference between the two eyelids outside normal mode.
    // The per-lid closure speed multiplier spans 0.7..1.3, so a mid-blink eye
    // can legitimately be up to ~0.6 of the base closure ahead of the other;
    // 0.40 covers that while still catching a genuinely desynchronized lid.
    constexpr float kMaxLidAsymmetry = 0.40F;
    SweepResult result;
    animator.setMode(mode, startUs);
    for (std::uint64_t now = startUs; now < startUs + durationUs; now += kFramePeriod) {
        const uncanny::EyeFrame left =
            animator.update(0, now, artwork, renderer, settings);
        const uncanny::EyeFrame right =
            animator.update(1, now, artwork, renderer, settings);

        // The gaze target and the iris are strictly shared: both eyes are two
        // views of one subject, so any disagreement there is a defect.
        //
        // The eyelids are deliberately NOT required to match. Drowsiness and
        // the sustained-stare modes both model real per-eye lid asymmetry --
        // a resting offset plus a per-lid closure speed -- so a difference up
        // to kMaxLidAsymmetry is the intended behaviour. What must never
        // happen is one eye fully closed while the other is wide open.
        if (left.gazeX != right.gazeX || left.gazeY != right.gazeY ||
            left.irisScale != right.irisScale ||
            left.currentPupil != right.currentPupil) {
            result.unsynchronized = true;
        }
        if (std::fabs(left.lidClosure - right.lidClosure) >
            (mode == uncanny::AnimationMode::Normal ? 0.0F : kMaxLidAsymmetry)) {
            result.unsynchronized = true;
        }

        if (left.lidClosure < -0.001F || left.lidClosure > 1.001F ||
            left.upperThreshold < 0 || left.upperThreshold > 255 ||
            left.lowerThreshold < 0 || left.lowerThreshold > 255 ||
            left.irisScale < artwork.irisMin || left.irisScale > artwork.irisMax) {
            result.outOfBounds = true;
        }

        result.minClosure = std::min(result.minClosure, left.lidClosure);
        result.maxClosure = std::max(result.maxClosure, left.lidClosure);
        result.minUpper = std::min(result.minUpper, left.upperThreshold);
        result.maxUpper = std::max(result.maxUpper, left.upperThreshold);
        result.minLower = std::min(result.minLower, left.lowerThreshold);
        result.maxLower = std::max(result.maxLower, left.lowerThreshold);
    }
    return result;
}

// Applies the eye classification, and forces the pupil geometry only when the
// operator actually asked for one. Without this split, selecting a design
// without --pupil-type would silently flatten that asset's baked pupil.
void applyEyeTypes(uncanny::EyeAnimator& animator, uncanny::IrisType irisType,
                   uncanny::PupilType pupilType, uncanny::Realism realism,
                   bool pupilTypeExplicit) {
    animator.setEyeTypes(irisType, realism);
    if (pupilTypeExplicit) {
        animator.setPupilType(pupilType);
    }
}

const std::array<uncanny::AnimationMode, 4> kAllModes{
    uncanny::AnimationMode::Normal, uncanny::AnimationMode::Dozing,
    uncanny::AnimationMode::Suspicious, uncanny::AnimationMode::Angry};

}  // namespace

int smokeTest(const uncanny::EyeArtwork& artwork, uncanny::AnimationMode mode,
              uncanny::IrisType irisType, uncanny::PupilType pupilType,
              uncanny::Realism realism, bool pupilTypeExplicit) {
    uncanny::EyeRenderer renderer(artwork);
    const uncanny::RenderSettings settings{true, false, artwork.irisMin, artwork.irisMax};
    constexpr std::uint64_t start = 1'000'000;

    // ── Renderer purity (TODO A2) ─────────────────────────────────────────
    // The renderer's only contract is that identical inputs give identical
    // pixels. This is what makes frames reproducible from state alone.
    {
        uncanny::RenderInput probe;
        probe.gazeX = artwork.scleraWidth / 2 - uncanny::kEyeSize / 2;
        probe.gazeY = artwork.scleraHeight / 2 - uncanny::kEyeSize / 2;
        probe.irisScale = 420;
        probe.upperThreshold = 128;
        probe.lowerThreshold = 0;
        const uncanny::EyeRender first = renderer.render(probe);
        const uncanny::EyeRender second = renderer.render(probe);
        if (first.rgba != second.rgba) {
            throw std::runtime_error("smoke test found a non-deterministic renderer");
        }
    }

    // ── Palette ───────────────────────────────────────────────────────────
    uncanny::EyeAnimator animator({true, true, false, 1});
    applyEyeTypes(animator, irisType, pupilType, realism, pupilTypeExplicit);
    std::size_t coloredPixels = 0;
    std::size_t redPixels = 0;
    std::size_t greenPixels = 0;
    std::size_t bluePixels = 0;
    for (int frame = 0; frame < 3; ++frame) {
        const std::uint64_t now = start + static_cast<std::uint64_t>(frame) * 16'667;
        for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
            const uncanny::EyeFrame eyeFrame =
                animator.update(eye, now, artwork, renderer, settings);
            for (std::size_t pixel = 0; pixel < eyeFrame.render.rgba.size(); pixel += 4) {
                if (eyeFrame.render.rgba[pixel + 3] != 0) {
                    ++coloredPixels;
                }
                if (eyeFrame.render.rgba[pixel] != 0) {
                    ++redPixels;
                }
                if (eyeFrame.render.rgba[pixel + 1] != 0) {
                    ++greenPixels;
                }
                if (eyeFrame.render.rgba[pixel + 2] != 0) {
                    ++bluePixels;
                }
            }
        }
    }
    const bool expectsBlue = artwork.name != "cat" && artwork.name != "customDragon";
    if (coloredPixels == 0 || redPixels == 0 || greenPixels == 0 ||
        (expectsBlue && bluePixels == 0)) {
        throw std::runtime_error("smoke test rendered an invalid eye palette");
    }

    // ── Shared gaze target at opposite extremes ───────────────────────────
    uncanny::EyeAnimator gazeAnimator({false, false, false, 1});
    gazeAnimator.setGaze(0, 0, 512);
    const std::uint64_t gazeStart = start + 1'000'000;
    const uncanny::EyeFrame gazeLeft0 =
        gazeAnimator.update(0, gazeStart, artwork, renderer, settings);
    const uncanny::EyeFrame gazeLeft1 =
        gazeAnimator.update(1, gazeStart, artwork, renderer, settings);
    if (gazeLeft0.gazeX != gazeLeft1.gazeX || gazeLeft0.gazeY != gazeLeft1.gazeY ||
        gazeLeft0.renderX >= gazeLeft1.renderX) {
        throw std::runtime_error("smoke test found independent eye gaze targets");
    }

    gazeAnimator.setGaze(0, 1023, 512);
    const std::uint64_t gazeEnd = gazeStart + 16'667;
    const uncanny::EyeFrame gazeRight0 =
        gazeAnimator.update(0, gazeEnd, artwork, renderer, settings);
    const uncanny::EyeFrame gazeRight1 =
        gazeAnimator.update(1, gazeEnd, artwork, renderer, settings);
    if (gazeRight0.gazeX != gazeRight1.gazeX || gazeRight0.gazeY != gazeRight1.gazeY ||
        gazeRight0.renderX <= gazeLeft0.renderX ||
        gazeLeft0.gazeX == gazeRight0.gazeX) {
        throw std::runtime_error("smoke test found a stale or unsynchronized gaze transition");
    }

    // The gaze clamp is deliberately limited to the artwork bounds: an explicit
    // gaze at either extreme must reach the legal limit of the coordinate range
    // and of the sclera crop, not stop short of it. The gaze path has no
    // distance envelope -- "minimal movement" is how often the gaze changes,
    // never how far the eye travels. The only illegal position is a crop that
    // slides out of the artwork.
    const int maxRenderX = artwork.scleraWidth - static_cast<int>(uncanny::kEyeSize);
    const int maxRenderY = artwork.scleraHeight - static_cast<int>(uncanny::kEyeSize);
    if (gazeLeft0.gazeX != 0 || gazeRight0.gazeX != kCoordinateMax ||
        gazeLeft0.renderX < 0 || gazeLeft0.renderX > maxRenderX ||
        gazeRight0.renderX != maxRenderX ||
        gazeRight0.renderY < 0 || gazeRight0.renderY > maxRenderY) {
        throw std::runtime_error(
            "smoke test found a gaze clamped short of the artwork bounds");
    }

    gazeAnimator.setGaze(0, 512, 0);
    const uncanny::EyeFrame gazeTop =
        gazeAnimator.update(0, gazeEnd + 16'667, artwork, renderer, settings);
    gazeAnimator.setGaze(0, 512, kCoordinateMax);
    const uncanny::EyeFrame gazeBottom =
        gazeAnimator.update(0, gazeEnd + 33'334, artwork, renderer, settings);
    if (gazeTop.gazeY != 0 || gazeBottom.gazeY != kCoordinateMax ||
        gazeTop.renderY != 0 || gazeBottom.renderY != maxRenderY) {
        throw std::runtime_error(
            "smoke test found a vertical gaze clamped short of the artwork bounds");
    }

    // The right eye's crop must mirror the left eye's exactly. This is the
    // invariant behind the display-layer horizontal flip: if it holds, the two
    // eyes move the same way in the world while keeping right-eye anatomy.
    // Comparing the deltas rather than the absolute values is deliberate --
    // the extremes sit at opposite ends of the same clamp range, so the mirror
    // cancels out of the difference.
    const int leftEyeDelta = gazeRight0.renderX - gazeLeft0.renderX;
    const int rightEyeDelta = gazeRight1.renderX - gazeLeft1.renderX;
    if (leftEyeDelta == 0 || leftEyeDelta != -rightEyeDelta) {
        throw std::runtime_error("smoke test found a broken right-eye crop mirror");
    }

    // ── Blink: complete, synchronized, and leaving no residue ──────────────
    uncanny::EyeAnimator blinkAnimator({false, true, false, 1});
    blinkAnimator.setGaze(0, 512, 512);
    const std::uint64_t blinkStart = gazeEnd + 5'000'000;
    blinkAnimator.blink(blinkStart);
    const std::uint64_t closingUs = blinkStart + 10'000;
    const uncanny::EyeFrame closing0 =
        blinkAnimator.update(0, closingUs, artwork, renderer, settings);
    const uncanny::EyeFrame closing1 =
        blinkAnimator.update(1, closingUs, artwork, renderer, settings);
    if (closing0.blinkState != closing1.blinkState || closing0.blinkState == 0 ||
        closing0.lidClosure <= 0.0F) {
        throw std::runtime_error("smoke test found unsynchronized blink entry");
    }

    // Run the blink to completion, then confirm both eyes have settled back to
    // the same open baseline with byte-identical frames.
    const std::uint64_t settledUs = blinkStart + 2'000'000;
    const uncanny::EyeFrame open0 =
        blinkAnimator.update(0, settledUs, artwork, renderer, settings);
    const uncanny::EyeFrame open1 =
        blinkAnimator.update(1, settledUs, artwork, renderer, settings);
    const uncanny::EyeFrame stable0 =
        blinkAnimator.update(0, settledUs, artwork, renderer, settings);
    const uncanny::EyeFrame stable1 =
        blinkAnimator.update(1, settledUs, artwork, renderer, settings);
    if (open0.blinkState != 0 || open1.blinkState != 0 ||
        open0.lidClosure > 0.001F || open1.lidClosure > 0.001F ||
        open0.upperThreshold != stable0.upperThreshold ||
        open0.lowerThreshold != stable0.lowerThreshold ||
        open1.upperThreshold != stable1.upperThreshold ||
        open1.lowerThreshold != stable1.lowerThreshold ||
        open0.render.rgba != stable0.render.rgba ||
        open1.render.rgba != stable1.render.rgba) {
        throw std::runtime_error("smoke test found blink residue after opening");
    }

    // ── Determinism (TODO A2) ─────────────────────────────────────────────
    // Two identically seeded animators fed the same timestamps must agree
    // pixel for pixel, otherwise no frame can be reproduced or regression
    // tested.
    {
        uncanny::EyeAnimator a({true, true, false, 7});
        uncanny::EyeAnimator b({true, true, false, 7});
        applyEyeTypes(a, irisType, pupilType, realism, pupilTypeExplicit);
        applyEyeTypes(b, irisType, pupilType, realism, pupilTypeExplicit);
        a.setMode(mode, start);
        b.setMode(mode, start);
        for (std::uint64_t now = start; now < start + 2'000'000; now += 16'667) {
            for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
                const uncanny::EyeFrame fa = a.update(eye, now, artwork, renderer, settings);
                const uncanny::EyeFrame fb = b.update(eye, now, artwork, renderer, settings);
                if (fa.render.rgba != fb.render.rgba || fa.irisScale != fb.irisScale ||
                    fa.upperThreshold != fb.upperThreshold) {
                    throw std::runtime_error(
                        "smoke test found a non-deterministic animator in mode " +
                        std::string(uncanny::modeName(mode)));
                }
            }
        }
    }

    // ── Every mode must start, stay bounded, and stay synchronized ────────
    // (TODO A1 exit criteria: all four modes are represented and runnable.)
    for (uncanny::AnimationMode candidate : kAllModes) {
        uncanny::EyeAnimator modeAnimator({true, true, false, 3});
        applyEyeTypes(modeAnimator, irisType, pupilType, realism, pupilTypeExplicit);
        const SweepResult result =
            sweep(modeAnimator, artwork, renderer, settings, candidate, start, 4'000'000);
        if (result.outOfBounds) {
            throw std::runtime_error("smoke test found out-of-range values in mode " +
                                     std::string(uncanny::modeName(candidate)));
        }
        if (result.unsynchronized) {
            throw std::runtime_error("smoke test found desynchronized eyes in mode " +
                                     std::string(uncanny::modeName(candidate)));
        }
        if (modeAnimator.mode() != candidate) {
            throw std::runtime_error("smoke test could not enter mode " +
                                     std::string(uncanny::modeName(candidate)));
        }
    }

    // ── Selected mode: behavioural expectations ───────────────────────────
    if (mode == uncanny::AnimationMode::Dozing) {
        // TODO A4: dozing has to be visibly dozing, not a fresh-boot state
        // that looks identical to normal. Require a real droop within a few
        // seconds of entering the mode.
        uncanny::EyeAnimator dozeAnimator({true, true, false, 1});
        const SweepResult result =
            sweep(dozeAnimator, artwork, renderer, settings, mode, start, 6'000'000);
        if (result.maxClosure < 0.15F) {
            throw std::runtime_error(
                "smoke test found an invisible dozing state; max lid closure was " +
                std::to_string(result.maxClosure));
        }
    }

    if (mode == uncanny::AnimationMode::Angry || mode == uncanny::AnimationMode::Suspicious) {
        // Both are sustained-stare modes: the resting lid must sit narrowed
        // relative to normal, and the blink rate must be visibly suppressed.
        //
        // The comparison is against each mode's *most open* observed lid, not
        // its most closed: a blink drives normal to full closure, so comparing
        // peak closure would rank normal as the narrowest of all and prove
        // nothing.
        uncanny::EyeAnimator stareAnimator({true, true, false, 1});
        const SweepResult stare =
            sweep(stareAnimator, artwork, renderer, settings, mode, start, 8'000'000);
        if (stare.maxClosure < 0.10F) {
            throw std::runtime_error(
                "smoke test found no lid narrowing in mode " +
                std::string(uncanny::modeName(mode)));
        }
        uncanny::EyeAnimator normalAnimator({true, true, false, 1});
        const SweepResult normal =
            sweep(normalAnimator, artwork, renderer, settings,
                  uncanny::AnimationMode::Normal, start, 8'000'000);
        if (stare.minUpper <= normal.minUpper) {
            throw std::runtime_error(
                "smoke test found mode " + std::string(uncanny::modeName(mode)) +
                " resting no narrower than normal (minUpper " +
                std::to_string(stare.minUpper) + " vs " +
                std::to_string(normal.minUpper) + ")");
        }
    }

    // ── Recovery: leaving a mode must restore a wide-open lid ─────────────
    if (mode == uncanny::AnimationMode::Dozing) {
        uncanny::EyeAnimator recoveryAnimator({true, true, false, 1});
        sweep(recoveryAnimator, artwork, renderer, settings,
              uncanny::AnimationMode::Dozing, start, 5'000'000);
        recoveryAnimator.setMode(uncanny::AnimationMode::Normal, start + 5'000'000);
        const uncanny::EyeFrame recovered =
            recoveryAnimator.update(0, start + 5'100'000, artwork, renderer, settings);
        // Compare the measured closure rather than an absolute threshold: how
        // wide the lid rests is artwork-dependent, but the closure the mode
        // contributes is not, and on mode exit it must be zero.
        if (recovered.lidClosure > 0.02F) {
            throw std::runtime_error(
                "smoke test found dozing eyelids failing to recover on mode exit: "
                "closure=" + std::to_string(recovered.lidClosure));
        }
    }

    std::cout << "smoke test: selected-mode gaze and synchronized blink checks passed\n";
    std::cout << "smoke test: all " << kAllModes.size()
              << " modes bounded, synchronized, and deterministic\n";
    std::cout << "smoke test: rendered " << coloredPixels << " opaque pixels "
              << "(red=" << redPixels << ", green=" << greenPixels << ", blue=" << bluePixels
              << ") across 6 eye frames\n";
    return 0;
}

int run(AppOptions options) {
    if (options.listDesigns) {
        printDesigns();
        return 0;
    }
    if (options.listModes) {
        printVocabulary({uncanny::modeName(uncanny::AnimationMode::Normal),
                         uncanny::modeName(uncanny::AnimationMode::Dozing),
                         uncanny::modeName(uncanny::AnimationMode::Suspicious),
                         uncanny::modeName(uncanny::AnimationMode::Angry)});
        return 0;
    }
    if (options.listIrisTypes) {
        printVocabulary({uncanny::irisTypeName(uncanny::IrisType::Mammalian),
                         uncanny::irisTypeName(uncanny::IrisType::Feline),
                         uncanny::irisTypeName(uncanny::IrisType::Reptilian),
                         uncanny::irisTypeName(uncanny::IrisType::Goat)});
        return 0;
    }
    if (options.listPupilTypes) {
        printVocabulary({uncanny::pupilTypeName(uncanny::PupilType::Round),
                         uncanny::pupilTypeName(uncanny::PupilType::VerticalSlit),
                         uncanny::pupilTypeName(uncanny::PupilType::HorizontalSlit)});
        return 0;
    }
    if (options.listRealism) {
        printVocabulary({uncanny::realismName(uncanny::Realism::Stylized),
                         uncanny::realismName(uncanny::Realism::Realistic),
                         uncanny::realismName(uncanny::Realism::Hyperreal)});
        return 0;
    }
    if (options.help) {
        printUsage("uncanny-eyes");
        return 0;
    }

    const uncanny::EyeArtwork& artwork = findArtwork(options.design);
    if (options.smokeTest) {
        return smokeTest(artwork, options.mode, options.irisType, options.pupilType,
                         options.realism, options.pupilTypeExplicit);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    if (options.faceOverlayPath.empty() || IMG_Init(IMG_INIT_PNG) == 0) {
        if (!options.faceOverlayPath.empty()) {
            std::cerr << "Warning: SDL_image PNG support unavailable: " << IMG_GetError() << "\n";
        }
    }

    if (options.streamMode) {
        // The capture geometry is the contract with the RTSP server: it is
        // fixed at 1920x1080 so the encoder, the window id and the remote
        // decoder never have to renegotiate a resolution.
        options.width = 1920;
        options.height = 1080;
    }

    const std::uint32_t flags = SDL_WINDOW_SHOWN |
        (options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0) |
        (options.borderless ? SDL_WINDOW_BORDERLESS : 0);
    SDL_Window* window = SDL_CreateWindow("Uncanny Eyes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           options.width, options.height, flags);
    if (window == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + error);
    }

    uncanny::EyeRenderer eyeRenderer(artwork);
    // autonomousIris is opt-in: with no LDR the pupil has no reason to move on
    // its own, so the default is a still pupil driven only by the mode.
    uncanny::EyeAnimator animator({true, true, options.autonomousIris, 1, options.ldr.enabled ? options.ldr.irisWeight : 0.0F,
                                   options.ldr.constrictionTime, options.ldr.dilationTime});
    animator.setDrowsinessConfig(options.drowsiness);
    // Apply the eye classification and realism level. When no explicit pupil
    // type was requested the animator keeps each asset's own baked pupil
    // geometry; the classification still constrains the dilation envelope.
    animator.setEyeTypes(options.irisType, options.realism);
    if (options.pupilTypeExplicit) {
        animator.setPupilType(options.pupilType);
    }
    if (options.irisScaleOverrideEnabled) {
        animator.setIrisScale(options.irisScaleOverride);
        animator.setEnvironmentalActive(false);
    }
    animator.setExpressionIntensity(options.expressionIntensity);
    animator.setLdrSensitivity(options.ldrSensitivity);
    uncanny::LDRSerialInput ldrInput(options.ldr);
    ldrInput.start();
    const uncanny::RenderSettings renderSettings{true, false, artwork.irisMin, artwork.irisMax};
    std::array<uncanny::EyeFrame, uncanny::kEyeCount> frames{};
    const std::uint64_t framePeriodUs = 1'000'000U / static_cast<std::uint64_t>(options.frameRate);
    std::uint64_t nextFrameUs = nowMicroseconds();
    bool modeInitialized = false;
    bool running = true;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == nullptr) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + error);
    }
    std::array<SDL_Texture*, uncanny::kEyeCount> textures{};
    for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
        textures[eye] = SDL_CreateTexture(
            renderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING,
            static_cast<int>(uncanny::kEyeSize), static_cast<int>(uncanny::kEyeSize));
        if (textures[eye] == nullptr) {
            const std::string error = SDL_GetError();
            for (SDL_Texture* texture : textures) {
                SDL_DestroyTexture(texture);
            }
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + error);
        }
    }
    SDL_Texture* faceOverlayTexture = nullptr;
    if (!options.faceOverlayPath.empty()) {
        SDL_Surface* overlaySurface = IMG_Load(options.faceOverlayPath.c_str());
        if (overlaySurface) {
            faceOverlayTexture = SDL_CreateTextureFromSurface(renderer, overlaySurface);
            SDL_FreeSurface(overlaySurface);
            if (faceOverlayTexture) {
                SDL_SetTextureBlendMode(faceOverlayTexture, SDL_BLENDMODE_BLEND);
                std::cout << "Loaded face overlay: " << options.faceOverlayPath
                          << " (" << overlaySurface->w << "x" << overlaySurface->h << ")\n";
            } else {
                std::cerr << "Failed to create face overlay texture: " << SDL_GetError() << "\n";
            }
        } else {
            std::cerr << "Failed to load face overlay: " << IMG_GetError() << "\n";
        }
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_f) {
                const std::uint32_t current = SDL_GetWindowFlags(window);
                if ((current & SDL_WINDOW_FULLSCREEN) != 0) {
                    SDL_SetWindowFullscreen(window, 0);
                } else {
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                }
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d) {
                // Cycle through every registered mode so all four are
                // reachable at runtime without a restart.
                const auto index = static_cast<std::size_t>(
                    std::find(kAllModes.begin(), kAllModes.end(), options.mode) -
                    kAllModes.begin());
                options.mode = kAllModes[(index + 1) % kAllModes.size()];
                modeInitialized = false;
                std::cout << "mode: " << uncanny::modeName(options.mode) << "\n";
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_l && options.ldr.enabled) {
                const uncanny::LDRSerialSample sample = ldrInput.snapshot();
                if (sample.connected && !sample.timedOut) {
                    std::cout << "LDR: connected\nPort: " << sample.port
                              << "\nRaw: " << sample.raw
                              << "\nFiltered: " << sample.filtered
                              << "\nNormalized: " << sample.normalized
                              << "\nTarget pupil: " << sample.targetPupil
                              << "\nCurrent pupil: " << animator.currentPupil() << "\n";
                } else {
                    std::cout << "LDR: disconnected\n";
                }
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_y &&
                       options.mode == uncanny::AnimationMode::Dozing) {
                const auto& ds = animator.drowsiness();
                std::cout << "Drowsiness: " << ds.drowsiness() << "\n"
                          << "Event: " << static_cast<int>(ds.currentEvent()) << "\n"
                          << "Left lid: " << ds.leftEyelidPosition() << "\n"
                          << "Right lid: " << ds.rightEyelidPosition() << "\n"
                          << "Gaze wander: " << ds.gazeWander() << "\n";
            }
        }

        const std::uint64_t currentUs = nowMicroseconds();
        if (currentUs >= nextFrameUs) {
            if (!modeInitialized) {
                animator.setMode(options.mode, currentUs);
                modeInitialized = true;
            }
            ldrInput.update();
            const uncanny::LDRSerialSample ldrSample = ldrInput.snapshot();
            // The animator owns the pupil response model. main.cpp only ever
            // hands over the light target, so the physiological timing,
            // deadband and hysteresis cannot be bypassed by the call site.
            const bool ldrLive = options.ldr.enabled && ldrSample.connected &&
                                 !ldrSample.timedOut;
            animator.setEnvironmentalActive(ldrLive);
            if (ldrLive) {
                animator.setEnvironmentalPupil(ldrSample.targetPupil, currentUs);
            }
            for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
                frames[eye] = animator.update(eye, currentUs, artwork, eyeRenderer, renderSettings);
            }
            nextFrameUs = currentUs + framePeriodUs;
        } else {
            const std::uint64_t sleepUs = nextFrameUs - currentUs;
            if (sleepUs > 1000) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepUs - 1000));
            }
            continue;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        SDL_SetRenderDrawColor(renderer, options.background[0], options.background[1], options.background[2], 255);
        SDL_RenderClear(renderer);

        const float baseEyeWidth = std::min(
            static_cast<float>(windowWidth) * kEyeWidthFraction,
            static_cast<float>(windowHeight) * kEyeHeightFraction);
        const float minimumGap = std::min(
            kMinimumGapPixels, static_cast<float>(windowWidth) * 0.05F);
        const float maxEyeWidth = std::max(1.0F,
            (static_cast<float>(windowWidth) - minimumGap) * 0.5F);
        const float eyeWidth = std::min(baseEyeWidth * options.scale, maxEyeWidth);
        const float eyeHeight = eyeWidth;
        const float scaledWidth = eyeWidth;
        const float scaledHeight = eyeHeight;
        const float availableGap = std::max(0.0F,
            static_cast<float>(windowWidth) - scaledWidth * 2.0F);
        const float gap = std::min(
            std::max(kMinimumGapPixels,
                     static_cast<float>(windowWidth) * kEyeGapFraction) * options.scale,
            availableGap);
        const float startX = (static_cast<float>(windowWidth) - scaledWidth * 2.0F - gap) * 0.5F;
        // Push eyes up: offset startY so eyes sit in the upper portion of the window
        // 0.5F = centered (default), 0.33F = 1/3 from top
        const float eyeVerticalBias = 0.40F;  // 40% from top = eyes higher
        const float startY = (static_cast<float>(windowHeight) - scaledHeight) * 0.5F -
                             (static_cast<float>(windowHeight) * (0.5F - eyeVerticalBias));

        for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
            void* pixels = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(textures[eye], nullptr, &pixels, &pitch) != 0) {
                continue;
            }
            const std::size_t rowBytes = static_cast<std::size_t>(pitch);
            auto* destination = static_cast<std::uint32_t*>(pixels);
            const std::uint8_t* source = frames[eye].render.rgba.data();
            for (std::size_t y = 0; y < uncanny::kEyeSize; ++y) {
                for (std::size_t x = 0; x < uncanny::kEyeSize; ++x) {
                    const std::size_t sourcePixel = (y * uncanny::kEyeSize + x) * 4;
                    const std::uint32_t red = source[sourcePixel];
                    const std::uint32_t green = source[sourcePixel + 1];
                    const std::uint32_t blue = source[sourcePixel + 2];
                    const std::uint32_t alpha = source[sourcePixel + 3];
                    destination[y * rowBytes / sizeof(*destination) + x] =
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                        (alpha << 24) | (red << 16) | (green << 8) | blue;
#else
                        (alpha << 24) | (blue << 16) | (green << 8) | red;
#endif
                }
            }
            SDL_UnlockTexture(textures[eye]);
            SDL_Rect destinationRect{
                static_cast<int>(startX + static_cast<float>(1 - eye) * (scaledWidth + gap)),
                static_cast<int>(startY), static_cast<int>(scaledWidth), static_cast<int>(scaledHeight)};
            SDL_RenderCopyEx(renderer, textures[eye], nullptr, &destinationRect,
                             0.0, nullptr,
                             eye == 1 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        // Render face overlay on top of eyes
        // The overlay PNG is 16:9 with transparent eye holes positioned to
        // match the rendered eyes. Scale to fill the entire window.
        if (faceOverlayTexture) {
            SDL_Rect overlayRect{0, 0, windowWidth, windowHeight};
            SDL_RenderCopy(renderer, faceOverlayTexture, nullptr, &overlayRect);
        }
        SDL_RenderPresent(renderer);
    }

    for (SDL_Texture* texture : textures) {
        SDL_DestroyTexture(texture);
    }
    if (faceOverlayTexture) {
        SDL_DestroyTexture(faceOverlayTexture);
    }
    ldrInput.stop();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        AppOptions options;
        parseOptions(argc, argv, options);
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << "uncanny-eyes: " << error.what() << "\n";
        return 1;
    }
}
