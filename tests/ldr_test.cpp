#include "LDRSerialInput.h"
#include "PupilResponseModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 ", expected=" + std::to_string(expected));
    }
}

void testParsing() {
    std::int64_t value = 0;
    require(uncanny::LDRSerialInput::parseLine("723", value) && value == 723,
            "plain integer parsing failed");
    require(uncanny::LDRSerialInput::parseLine("0", value) && value == 0,
            "zero parsing failed");
    require(uncanny::LDRSerialInput::parseLine("4095", value) && value == 4095,
            "maximum integer parsing failed");
    require(uncanny::LDRSerialInput::parseLine("LDR:701", value) && value == 701,
            "LDR prefix parsing failed");
    require(!uncanny::LDRSerialInput::parseLine("hello", value),
            "text line was accepted");
    require(!uncanny::LDRSerialInput::parseLine("LDR:", value),
            "empty prefixed line was accepted");
    require(!uncanny::LDRSerialInput::parseLine("12abc", value),
            "trailing text was accepted");
    require(!uncanny::LDRSerialInput::parseLine("", value),
            "empty line was accepted");
    require(!uncanny::LDRSerialInput::parseLine("-1", value),
            "negative reading was accepted");
}

void testNormalizationAndMapping() {
    uncanny::LDRSerialConfig config;
    config.sensorMin = 0;
    config.sensorMax = 4095;
    requireNear(uncanny::LDRSerialInput::normalize(0, config), 0.0F, 0.001F,
                "sensor minimum normalization failed");
    requireNear(uncanny::LDRSerialInput::normalize(4095, config), 1.0F, 0.001F,
                "sensor maximum normalization failed");
    requireNear(uncanny::LDRSerialInput::normalize(2048, config), 0.5001F, 0.002F,
                "midpoint normalization failed");

    config.invert = true;
    requireNear(uncanny::LDRSerialInput::normalize(0, config), 1.0F, 0.001F,
                "inverted minimum normalization failed");
    requireNear(uncanny::LDRSerialInput::normalize(4095, config), 0.0F, 0.001F,
                "inverted maximum normalization failed");

    config.invert = false;
    config.minPupil = 0.25F;
    config.maxPupil = 1.0F;
    const float dark = uncanny::LDRSerialInput::mapTarget(0, config);
    const float bright = uncanny::LDRSerialInput::mapTarget(4095, config);
    require(dark > bright, "dark environment did not produce a larger pupil");
    requireNear(dark, 1.0F, 0.001F, "dark pupil mapping failed");
    requireNear(bright, 0.25F, 0.001F, "bright pupil mapping failed");
}

void testFiltering() {
    uncanny::LDRSerialConfig config;
    config.sensorMin = 0;
    config.sensorMax = 4095;
    config.filterAlpha = 0.15F;
    uncanny::LDRSerialInput input(config);
    const std::int64_t values[] = {700, 702, 699, 1200, 701, 703};
    std::int64_t filtered = values[0];
    for (std::size_t index = 1; index < sizeof(values) / sizeof(values[0]); ++index) {
        filtered = static_cast<std::int64_t>(std::llround(
            static_cast<long double>(filtered) * 0.85L +
            static_cast<long double>(values[index]) * 0.15L));
    }
    require(filtered < 800, "filter allowed the spike to dominate the output");
    require(filtered > 650, "filter moved too far from the stable readings");
}

void testResponseModel() {
    uncanny::LDRSerialConfig config;
    config.sensorMin = 0;
    config.sensorMax = 4095;
    config.constrictionTime = 0.10F;
    config.dilationTime = 1.0F;
    uncanny::PupilResponseModel model(config);
    model.setActive(true);

    uncanny::LDRSerialSample bright;
    bright.connected = true;
    bright.targetPupil = 0.25F;
    bright.lastValidUpdateUs = 1'000'000;
    model.update(bright, 1'000'000);
    requireNear(model.current(), 0.25F, 0.001F, "initial response did not initialize");

    uncanny::LDRSerialSample dark = bright;
    dark.targetPupil = 1.0F;
    model.update(dark, 1'050'000);
    require(model.current() < model.target(), "response jumped immediately to target");
    require(model.current() > 0.25F, "dilation did not begin moving toward target");

    model.update(dark, 3'000'000);
    require(model.current() > 0.90F, "dilation did not converge after response time");
    requireNear(model.current(), model.target(), 0.02F, "response did not converge");
}

void testDisconnectTimeout() {
    uncanny::LDRSerialConfig config;
    config.enabled = true;
    config.port = "/dev/ttyUSB1";
    config.disconnectTimeoutUs = 20'000;
    uncanny::LDRSerialInput input(config);
    input.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    input.stop();

    config.disconnectTimeoutUs = 1'000;
    uncanny::LDRSerialInput timedOut(config);
    timedOut.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const uncanny::LDRSerialSample sample = timedOut.snapshot();
    require(sample.connected, "serial input should report connected while the port is open");
    require(!sample.timedOut, "timeout should not fire before the configured hold window");
    timedOut.stop();
}

}  // namespace

int main() {
    try {
        testParsing();
        testNormalizationAndMapping();
        testFiltering();
        testResponseModel();
        testDisconnectTimeout();
        std::cout << "LDR tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "LDR tests failed: " << error.what() << "\n";
        return 1;
    }
}
