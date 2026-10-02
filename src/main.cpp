#include "LDRSerialInput.h"
#include "DrowsinessModel.h"
#include "artwork.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
    bool smokeTest = false;
    int width = 1280;
    int height = 720;
    std::string design = "default";
    uncanny::AnimationMode mode = uncanny::AnimationMode::Normal;
    float scale = 1.0F;
    std::array<std::uint8_t, 3> background{0, 0, 0};
    int frameRate = 60;
    bool listDesigns = false;
    bool help = false;
    uncanny::LDRSerialConfig ldr;
    uncanny::DrowsinessConfig drowsiness;
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
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--design" && i + 1 < argc) {
            options.design = lowerCopy(argv[++i]);
        } else if (argument == "--mode" && i + 1 < argc) {
            const std::string modeName = lowerCopy(argv[++i]);
            if (modeName == "normal") {
                options.mode = uncanny::AnimationMode::Normal;
            } else if (modeName == "dozing") {
                options.mode = uncanny::AnimationMode::Dozing;
            } else {
                throw std::runtime_error("invalid --mode value; use normal or dozing");
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
        } else if (argument == "--borderless") {
            options.borderless = true;
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
              << "  --smoke-test             Render a few frames and exit without opening a window\n"
              << "  --width PIXELS           Window width (default: 1280)\n"
              << "  --height PIXELS          Window height (default: 720)\n"
              << "  --design NAME            Available designs: " << designs << "\n"
              << "  --mode NAME              Animation mode: normal or dozing (default: normal)\n"
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
              << "  --list-designs           Print available eye designs and exit\n"
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

std::uint64_t nowMicroseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

int smokeTest(const uncanny::EyeArtwork& artwork, uncanny::AnimationMode mode) {
    uncanny::EyeRenderer renderer(artwork);
    uncanny::EyeAnimator animator({true, true, true, 1});
    const uncanny::RenderSettings settings{true, false, artwork.irisMin, artwork.irisMax};
    constexpr std::uint64_t start = 1'000'000;
    std::size_t coloredPixels = 0;
    std::size_t redPixels = 0;
    std::size_t greenPixels = 0;
    std::size_t bluePixels = 0;
    for (int frame = 0; frame < 3; ++frame) {
        const std::uint64_t now = start + static_cast<std::uint64_t>(frame) * 16'667;
        for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
            const uncanny::EyeFrame eyeFrame = animator.update(eye, now, 0, artwork, renderer, settings);
            for (std::size_t pixel = 0; pixel < eyeFrame.render.rgba.size(); pixel += 4) {
                const std::uint8_t red = eyeFrame.render.rgba[pixel];
                const std::uint8_t green = eyeFrame.render.rgba[pixel + 1];
                const std::uint8_t blue = eyeFrame.render.rgba[pixel + 2];
                const std::uint8_t alpha = eyeFrame.render.rgba[pixel + 3];
                if (alpha != 0) {
                    ++coloredPixels;
                }
                if (red != 0) {
                    ++redPixels;
                }
                if (green != 0) {
                    ++greenPixels;
                }
                if (blue != 0) {
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

    // Exercise the shared gaze target at opposite extremes. The right eye uses
    // the mirrored crop coordinate required by its horizontally flipped display.
    uncanny::EyeAnimator gazeAnimator({false, false, true, 1});
    gazeAnimator.setGaze(0, 0, 512);
    const std::uint64_t gazeStart = start + 1'000'000;
    const uncanny::EyeFrame gazeLeft0 =
        gazeAnimator.update(0, gazeStart, 0, artwork, renderer, settings);
    const uncanny::EyeFrame gazeLeft1 =
        gazeAnimator.update(1, gazeStart, 0, artwork, renderer, settings);
    if (gazeLeft0.gazeX != gazeLeft1.gazeX || gazeLeft0.gazeY != gazeLeft1.gazeY ||
        gazeLeft0.renderX >= gazeLeft1.renderX) {
        throw std::runtime_error("smoke test found independent eye gaze targets");
    }

    gazeAnimator.setGaze(0, 1023, 512);
    const std::uint64_t gazeEnd = gazeStart + 16'667;
    const uncanny::EyeFrame gazeRight0 =
        gazeAnimator.update(0, gazeEnd, 0, artwork, renderer, settings);
    const uncanny::EyeFrame gazeRight1 =
        gazeAnimator.update(1, gazeEnd, 0, artwork, renderer, settings);
    if (gazeRight0.gazeX != gazeRight1.gazeX || gazeRight0.gazeY != gazeRight1.gazeY ||
        gazeRight0.renderX <= gazeRight1.renderX ||
        gazeLeft0.gazeX == gazeRight0.gazeX) {
        throw std::runtime_error("smoke test found a stale or unsynchronized gaze transition");
    }

    // Force one complete blink and verify both eyes leave it with identical,
    // stable thresholds. This catches shared-state and partial-texture residue.
    uncanny::EyeAnimator blinkAnimator({false, true, false, 1});
    blinkAnimator.setGaze(0, 512, 512);
    const std::uint64_t blinkStart = gazeEnd + 5'000'000;
    blinkAnimator.blink(blinkStart);
    const std::uint64_t closingUs = blinkStart + 10'000;
    const uncanny::EyeFrame closing0 =
        blinkAnimator.update(0, closingUs, 0, artwork, renderer, settings);
    const uncanny::EyeFrame closing1 =
        blinkAnimator.update(1, closingUs, 0, artwork, renderer, settings);
    if (closing0.blinkState != closing1.blinkState || closing0.blinkState == 0) {
        throw std::runtime_error("smoke test found unsynchronized blink entry");
    }

    const std::uint64_t openingUs = blinkStart + 500'000;
    const uncanny::EyeFrame opening0 =
        blinkAnimator.update(0, openingUs, 0, artwork, renderer, settings);
    const uncanny::EyeFrame opening1 =
        blinkAnimator.update(1, openingUs, 0, artwork, renderer, settings);
    const std::uint64_t openUs = openingUs + 200'000;
    const uncanny::EyeFrame open0 =
        blinkAnimator.update(0, openUs, 0, artwork, renderer, settings);
    const uncanny::EyeFrame open1 =
        blinkAnimator.update(1, openUs, 0, artwork, renderer, settings);
    const uncanny::EyeFrame stable0 =
        blinkAnimator.update(0, openUs, 0, artwork, renderer, settings);
    const uncanny::EyeFrame stable1 =
        blinkAnimator.update(1, openUs, 0, artwork, renderer, settings);
    if (opening0.blinkState == 0 || opening0.blinkState != opening1.blinkState ||
        open0.blinkState != 0 || open1.blinkState != 0 ||
        open0.upperThreshold != stable0.upperThreshold ||
        open0.lowerThreshold != stable0.lowerThreshold ||
        open1.upperThreshold != stable1.upperThreshold ||
        open1.lowerThreshold != stable1.lowerThreshold ||
        open0.render.rgba != stable0.render.rgba ||
        open1.render.rgba != stable1.render.rgba) {
        throw std::runtime_error("smoke test found blink residue after opening");
    }

    if (mode == uncanny::AnimationMode::Dozing) {
        const std::uint64_t breathPeriod = 12'000'000;
        const std::uint64_t halfCycle = breathPeriod / 2;
        const std::uint64_t cycleStart = start + breathPeriod;
        const std::uint64_t blinkStart = cycleStart + 18'000'000;
        const std::uint64_t closeDuration = 320'000;
        constexpr std::uint64_t framePeriod = 16'667;
        uncanny::EyeAnimator dozeAnimator({false, false, false, 1});
        dozeAnimator.setMode(mode, start);
        for (std::uint64_t now = start; now < cycleStart; now += framePeriod) {
            for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
                dozeAnimator.update(eye, now, 0, artwork, renderer, settings);
            }
        }
        const uncanny::EyeFrame closed0 =
            dozeAnimator.update(0, cycleStart, 0, artwork, renderer, settings);
        const uncanny::EyeFrame closed1 =
            dozeAnimator.update(1, cycleStart, 0, artwork, renderer, settings);
        if (dozeAnimator.mode() != uncanny::AnimationMode::Dozing ||
            closed0.gazeX != closed1.gazeX || closed0.gazeY != closed1.gazeY ||
            closed0.upperThreshold != closed1.upperThreshold ||
            closed0.lowerThreshold != closed1.lowerThreshold) {
            throw std::runtime_error("smoke test found invalid doze initialization");
        }

        const uncanny::EyeFrame breathing0 =
            dozeAnimator.update(0, cycleStart + halfCycle, 0, artwork, renderer, settings);
        const uncanny::EyeFrame breathing1 =
            dozeAnimator.update(1, cycleStart + halfCycle, 0, artwork, renderer, settings);
        if (breathing0.gazeX != breathing1.gazeX || breathing0.gazeY != breathing1.gazeY ||
            breathing0.upperThreshold != breathing1.upperThreshold ||
            breathing0.lowerThreshold != breathing1.lowerThreshold ||
            breathing0.upperThreshold == closed0.upperThreshold ||
            breathing0.lowerThreshold == closed0.lowerThreshold) {
            throw std::runtime_error("smoke test found incorrect doze breathing phase");
        }

        const uncanny::EyeFrame fullCycle0 =
            dozeAnimator.update(0, cycleStart + breathPeriod, 0, artwork, renderer, settings);
        const uncanny::EyeFrame fullCycle1 =
            dozeAnimator.update(1, cycleStart + breathPeriod, 0, artwork, renderer, settings);
        if (fullCycle0.gazeX != closed0.gazeX || fullCycle0.gazeY != closed0.gazeY ||
            fullCycle1.gazeX != closed1.gazeX || fullCycle1.gazeY != closed1.gazeY ||
            fullCycle0.upperThreshold != closed0.upperThreshold ||
            fullCycle0.lowerThreshold != closed0.lowerThreshold ||
            fullCycle1.upperThreshold != closed1.upperThreshold ||
            fullCycle1.lowerThreshold != closed1.lowerThreshold) {
            throw std::runtime_error(
                "smoke test found a non-periodic doze cycle: "
                "initial=(" + std::to_string(closed0.upperThreshold) +
                "," + std::to_string(closed0.lowerThreshold) +
                "), final=(" + std::to_string(fullCycle0.upperThreshold) +
                "," + std::to_string(fullCycle0.lowerThreshold) + ")");
        }

        const uncanny::EyeFrame drifting =
            dozeAnimator.update(0, cycleStart + breathPeriod / 4, 0, artwork, renderer, settings);
        if (drifting.gazeX < 480 || drifting.gazeX > 544 ||
            drifting.gazeY < 496 || drifting.gazeY > 528) {
            throw std::runtime_error("smoke test found excessive doze gaze movement");
        }

        const uncanny::EyeFrame blinkEntry0 =
            dozeAnimator.update(0, blinkStart, 0, artwork, renderer, settings);
        const uncanny::EyeFrame blinkEntry1 =
            dozeAnimator.update(1, blinkStart, 0, artwork, renderer, settings);
        if (blinkEntry0.blinkState != 1 || blinkEntry0.blinkState != blinkEntry1.blinkState ||
            blinkEntry0.upperThreshold != blinkEntry1.upperThreshold ||
            blinkEntry0.lowerThreshold != blinkEntry1.lowerThreshold) {
            throw std::runtime_error(
                "smoke test found unsynchronized doze blink entry: state=" +
                std::to_string(blinkEntry0.blinkState) + "/" +
                std::to_string(blinkEntry1.blinkState));
        }

        const uncanny::EyeFrame delayed0 =
            dozeAnimator.update(0, blinkStart + closeDuration + 1'000'000, 0, artwork, renderer, settings);
        const uncanny::EyeFrame delayed1 =
            dozeAnimator.update(1, blinkStart + closeDuration + 1'000'000, 0, artwork, renderer, settings);
        if (delayed0.blinkState != 0 || delayed0.blinkState != delayed1.blinkState ||
            delayed0.upperThreshold != delayed1.upperThreshold ||
            delayed0.lowerThreshold != delayed1.lowerThreshold) {
            throw std::runtime_error("smoke test found unstable post-blink doze state");
        }

        const uncanny::EyeFrame deadline0 =
            dozeAnimator.update(0, blinkStart + 18'000'000, 0, artwork, renderer, settings);
        const uncanny::EyeFrame deadline1 =
            dozeAnimator.update(1, blinkStart + 18'000'000, 0, artwork, renderer, settings);
        if (deadline0.blinkState != 1 || deadline0.blinkState != deadline1.blinkState) {
            throw std::runtime_error("smoke test found deadline drift in doze blinks");
        }
    }

    std::cout << "smoke test: selected-mode gaze and synchronized blink checks passed\n";
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
    if (options.help) {
        printUsage("uncanny-eyes");
        return 0;
    }

    const uncanny::EyeArtwork& artwork = findArtwork(options.design);
    if (options.smokeTest) {
        return smokeTest(artwork, options.mode);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
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
    uncanny::EyeAnimator animator({true, true, true, 1, options.ldr.enabled ? options.ldr.irisWeight : 0.0F,
                                   options.ldr.constrictionTime, options.ldr.dilationTime});
    animator.setDrowsinessConfig(options.drowsiness);
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
                const uncanny::AnimationMode nextMode =
                    (options.mode == uncanny::AnimationMode::Dozing)
                        ? uncanny::AnimationMode::Normal
                        : uncanny::AnimationMode::Dozing;
                options.mode = nextMode;
                modeInitialized = false;
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
                          << "Head pitch: " << ds.headPitch() << "\n"
                          << "Head yaw: " << ds.headYaw() << "\n"
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
            if (ldrSample.connected && !ldrSample.timedOut) {
                animator.setEnvironmentalPupil(ldrSample.targetPupil, currentUs);
            }
            for (std::size_t eye = 0; eye < uncanny::kEyeCount; ++eye) {
                frames[eye] = animator.update(eye, currentUs, 0, artwork, eyeRenderer, renderSettings);
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
            SDL_RenderCopyEx(renderer, textures[eye], nullptr, &destinationRect, 0.0,
                             nullptr, eye == 1 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        SDL_RenderPresent(renderer);
    }

    for (SDL_Texture* texture : textures) {
        SDL_DestroyTexture(texture);
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
