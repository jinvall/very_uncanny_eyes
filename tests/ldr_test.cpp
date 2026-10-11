#include "LDRSerialInput.h"
#include "PupilResponseModel.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

// A pseudo-terminal pair standing in for the LDR hardware.
//
// The reader used to be pointed at a real /dev/ttyUSB* device, which made this
// test pass or fail depending on whether that device existed, was plugged in,
// and was not already held open by the running application. That made a unit
// test fail for reasons that have nothing to do with the code under test. A PTY
// is a real serial device as far as the reader is concerned -- it is opened,
// configured with tcsetattr and polled for input exactly the same way -- but it
// is always present and always free.
class FakeSerialDevice {
public:
    FakeSerialDevice() {
        master_ = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master_ < 0 || ::grantpt(master_) != 0 || ::unlockpt(master_) != 0) {
            if (master_ >= 0) {
                ::close(master_);
                master_ = -1;
            }
            throw std::runtime_error("posix_openpt failed; the LDR test needs a PTY");
        }
        const char* const name = ::ptsname(master_);
        if (name == nullptr) {
            ::close(master_);
            master_ = -1;
            throw std::runtime_error("ptsname failed");
        }
        path_ = name;
        // The reader opens the slave side by path, so the test keeps the master
        // open and releases the slave descriptor it got implicitly.
        slave_ = ::open(path_.c_str(), O_RDWR | O_NOCTTY);
    }

    ~FakeSerialDevice() {
        if (master_ >= 0) {
            ::close(master_);
        }
        if (slave_ >= 0) {
            ::close(slave_);
        }
    }

    FakeSerialDevice(const FakeSerialDevice&) = delete;
    FakeSerialDevice& operator=(const FakeSerialDevice&) = delete;

    const std::string& path() const { return path_; }

    // Feeds one line to the reader, exactly as the sensor would.
    void writeLine(const std::string& line) {
        const std::string payload = line + "\n";
        ssize_t written = 0;
        while (written < static_cast<ssize_t>(payload.size())) {
            const ssize_t result =
                ::write(master_, payload.data() + written, payload.size() - written);
            if (result <= 0) {
                throw std::runtime_error("failed to write to the fake serial device");
            }
            written += result;
        }
    }

private:
    int master_ = -1;
    int slave_ = -1;
    std::string path_;
};

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
    // An over-long line must be rejected rather than accumulating.
    require(!uncanny::LDRSerialInput::parseLine(std::string(600, '7'), value),
            "over-long line was accepted");
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

    // Out-of-range ADC readings must clamp rather than wrap or extrapolate.
    requireNear(uncanny::LDRSerialInput::normalize(-500, config), 0.0F, 0.001F,
                "below-range reading did not clamp");
    requireNear(uncanny::LDRSerialInput::normalize(99999, config), 1.0F, 0.001F,
                "above-range reading did not clamp");
}

void testResponseModel() {
    uncanny::PupilResponseConfig config;
    config.constrictionTime = 0.10F;
    config.dilationTime = 1.0F;
    config.deadband = 0.0F;
    config.hysteresis = 0.0F;
    config.adaptationRate = 0.0F;
    uncanny::PupilResponseModel model(config);

    // The first sample snaps to the target instead of easing in from rest, so
    // the eyes do not visibly dilate on startup.
    model.update(0.25F, true, 1'000'000);
    requireNear(model.current(), 0.25F, 0.001F, "initial response did not initialize");
    require(!model.moving(), "a freshly initialized model reported movement");

    // Darker light (larger target) must dilate, and must not jump instantly.
    model.update(1.0F, true, 1'050'000);
    require(model.moving(), "dilation did not report movement");
    require(model.current() < model.target(), "response jumped immediately to target");
    require(model.current() > 0.25F, "dilation did not begin moving toward target");
    require(model.state().direction == 1, "dilation direction was not reported as +1");

    model.update(1.0F, true, 3'000'000);
    require(model.current() > 0.90F, "dilation did not converge after response time");
    requireNear(model.current(), model.target(), 0.02F, "response did not converge");
    require(!model.moving(), "a settled model still reported movement");

    // Brighter light must constrict faster than the dilation time constant.
    model.update(0.2F, true, 3'050'000);
    require(model.state().direction == -1, "constriction direction was not reported as -1");
    model.update(0.2F, true, 3'100'000);
    require(model.current() < 0.35F, "constriction did not use the faster time constant");
}

// TODO B3: the response model must not be a bare ADC pass-through.
void testDeadbandAndHysteresis() {
    uncanny::PupilResponseConfig config;
    config.constrictionTime = 0.10F;
    config.dilationTime = 0.10F;
    config.deadband = 0.05F;
    config.hysteresis = 0.05F;
    config.adaptationRate = 0.0F;
    uncanny::PupilResponseModel model(config);
    model.update(0.50F, true, 1'000'000);
    requireNear(model.current(), 0.50F, 0.001F, "deadband baseline failed");

    // Inside the deadband the pupil must not move at all.
    model.update(0.52F, true, 2'000'000);
    requireNear(model.current(), 0.50F, 0.001F, "deadband did not suppress small changes");

    // A real change must still get through.
    model.update(0.80F, true, 2'100'000);
    require(model.current() > 0.50F, "a change beyond the deadband was suppressed");

    // Settle, then verify a reversal smaller than the hysteresis does not
    // immediately flip the response back.
    model.update(0.80F, true, 4'000'000);
    requireNear(model.current(), 0.80F, 0.01F, "model did not settle before hysteresis test");
    model.update(0.78F, true, 4'100'000);
    requireNear(model.state().target, 0.80F, 0.01F,
                "hysteresis did not hold the target against a small reversal");
    model.update(0.60F, true, 4'200'000);
    requireNear(model.state().target, 0.60F, 0.01F,
                "hysteresis blocked a decisive reversal");
}

void testAdaptation() {
    uncanny::PupilResponseConfig config;
    config.constrictionTime = 0.05F;
    config.dilationTime = 0.05F;
    config.deadband = 0.0F;
    config.hysteresis = 0.0F;
    config.adaptationRate = 0.5F;
    uncanny::PupilResponseModel model(config);
    model.update(0.9F, true, 1'000'000);
    const float initialAdaptation = model.state().adaptation;
    model.update(0.9F, true, 3'000'000);
    require(model.state().adaptation > initialAdaptation,
            "light adaptation did not drift toward the sustained level");
}

void testInactiveRelaxation() {
    uncanny::PupilResponseConfig config;
    config.constrictionTime = 0.05F;
    config.dilationTime = 0.05F;
    config.deadband = 0.0F;
    config.hysteresis = 0.0F;
    config.adaptationRate = 0.0F;
    config.restingPupil = 0.5F;
    uncanny::PupilResponseModel model(config);
    model.update(1.0F, true, 1'000'000);
    requireNear(model.current(), 1.0F, 0.001F, "failed to reach the dilated start");

    // A disconnected sensor must relax to rest rather than strand the eye wide.
    for (std::uint64_t now = 1'050'000; now < 5'000'000; now += 50'000) {
        model.update(0.0F, false, now);
    }
    requireNear(model.current(), 0.5F, 0.01F,
                "an inactive sensor did not relax the pupil to rest");
}

void testReaderOverPseudoTerminal() {
    FakeSerialDevice device;
    uncanny::LDRSerialConfig config;
    config.enabled = true;
    config.port = device.path();
    config.sensorMin = 0;
    config.sensorMax = 4095;
    config.filterAlpha = 0.15F;
    config.disconnectTimeoutUs = 2'000'000;

    uncanny::LDRSerialInput input(config);
    input.start();

    // The reader opens on a 100 ms poll cycle, so give it room to connect.
    const std::uint64_t deadline = 3'000'000;
    std::uint64_t waited = 0;
    while (waited < deadline && !input.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10'000;
    }
    require(input.isConnected(), "the reader never opened the pseudo-terminal");
    require(input.snapshot().connected,
            "the reader did not publish the connection");

    device.writeLine("4095");
    bool received = false;
    waited = 0;
    while (waited < deadline && !received) {
        input.update();
        const uncanny::LDRSerialSample sample = input.snapshot();
        received = sample.lastValidUpdateUs != 0;
        if (!received) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            waited += 5'000;
        }
    }
    const uncanny::LDRSerialSample sample = input.snapshot();
    require(received, "the reader never parsed a line from the pseudo-terminal");
    require(sample.raw == 4095, "the raw reading was not published");
    // ADC max is the bright end in this wiring; --ldr-invert flips the
    // convention for sensors wired the other way round.
    requireNear(sample.normalized, 1.0F, 0.001F, "the reading was not normalized");
    requireNear(sample.targetPupil, 0.25F, 0.001F,
                "a fully bright reading did not map to a constricted pupil");
    require(!sample.timedOut, "a live device reported a timeout");

    // Dark must drive the pupil wider: a larger target than the 0.25 that the
    // bright reading produced. The readings have to be fed repeatedly because
    // the reader low-pass filters them, which is exactly the anti-jitter
    // behaviour the filter exists for.
    for (int attempt = 0; attempt < 60 && input.snapshot().targetPupil <= 0.5F;
         ++attempt) {
        device.writeLine("0");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(input.snapshot().targetPupil > 0.5F,
            "a dark reading did not produce a larger pupil than a bright one");

    // Malformed input must be ignored: it may not corrupt the last good reading
    // and it may not drop the connection. Comparing the raw value rather than a
    // timestamp keeps this robust while any already-queued valid samples are
    // still draining -- those legitimately carry the same raw value anyway.
    const std::int64_t lastGoodRaw = input.snapshot().raw;
    device.writeLine("not-a-number");
    device.writeLine("99999999999999999999999999");
    device.writeLine("-5");
    device.writeLine(std::string(600, '7'));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    input.update();
    require(input.snapshot().raw == lastGoodRaw,
            "a malformed line corrupted the last good reading");
    require(input.isConnected(), "malformed input disconnected the reader");

    input.stop();
    require(!input.snapshot().connected, "stopping the reader left it marked connected");
}

// A device that stays open but stops sending must release the environmental
    // modifier once the hold window expires, even though the port is still
    // open. This is the documented "connected but silent" behaviour.
void testConnectedButSilentTimeout() {
    FakeSerialDevice device;
    uncanny::LDRSerialConfig config;
    config.enabled = true;
    config.port = device.path();
    config.disconnectTimeoutUs = 50'000;  // 50 ms hold
    uncanny::LDRSerialInput input(config);
    input.start();

    std::uint64_t waited = 0;
    while (waited < 3'000'000 && !input.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        waited += 5'000;
    }
    require(input.isConnected(), "the reader never opened the pseudo-terminal");

    device.writeLine("2048");
    waited = 0;
    while (waited < 3'000'000 && input.snapshot().lastValidUpdateUs == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        waited += 5'000;
    }
    require(input.snapshot().lastValidUpdateUs != 0, "no reading was ever received");

    // Still inside the hold window: must still be live.
    input.update();
    require(input.snapshot().connected && !input.snapshot().timedOut,
            "the hold window expired before its configured duration");

    // Now let it go silent past the hold window.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    input.update();
    const uncanny::LDRSerialSample sample = input.snapshot();
    require(sample.timedOut, "a silent device did not release the hold window");
    require(!sample.connected, "a timed-out device was still reported connected");
    input.stop();
    require(!input.snapshot().connected, "stopping the reader left it marked connected");
}

void testFirstReadingIsHonoured() {
    // Regression: the first light reading must be adopted outright. On the very
    // first sample the raw delta is zero by definition, so if the deadband is
    // applied before initialisation it classifies the reading as "no change"
    // and leaves the pupil at its resting value -- the sensor is then ignored
    // until the light happens to move by more than the deadband.
    const float resting = 0.5F;
    for (float first : {0.05F, 0.25F, 0.75F, 0.95F}) {
        uncanny::PupilResponseConfig config;
        config.deadband = 0.05F;
        config.hysteresis = 0.05F;
        config.restingPupil = resting;
        uncanny::PupilResponseModel model(config);
        model.update(first, true, 1'000'000);
        requireNear(model.target(), first, 0.001F,
                    "the first light reading was not adopted (target)");
        requireNear(model.current(), first, 0.001F,
                    "the first light reading was not adopted (current)");
    }
}

// The light response must be monotonic and correctly oriented: brighter light
// constricts the pupil and darker light dilates it.
void testLightResponseIsMonotonicAndCorrectlyOriented() {
    uncanny::PupilResponseConfig config;
    config.deadband = 0.0F;
    config.hysteresis = 0.0F;
    config.adaptationRate = 0.0F;
    uncanny::PupilResponseModel model(config);

    // `openness` runs 0 (constricted, bright) to 1 (dilated, dark). irisScale
    // runs the other way, so wider openness must map to a larger irisScale.
    std::vector<std::pair<float, float>> seen;
    std::uint64_t now = 1'000'000;
    for (float openness : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        // The response is time based, so the clock has to advance between
        // steps for the pupil to actually travel to the new target.
        for (int step = 0; step < 40; ++step) {
            model.update(openness, true, now);
            now += 50'000;
        }
        model.update(openness, true, now);
        now += 50'000;
        requireNear(model.current(), openness, 0.01F,
                    "light target was not reached at openness " +
                        std::to_string(openness));
        seen.emplace_back(openness, model.current());
    }
    for (std::size_t i = 1; i < seen.size(); ++i) {
        require(seen[i].second > seen[i - 1].second,
                "pupil response is not monotonic in light level");
    }
}

}  // namespace

int main() {
    try {
        testParsing();
        testNormalizationAndMapping();
        testResponseModel();
        testDeadbandAndHysteresis();
        testAdaptation();
        testInactiveRelaxation();
        testFirstReadingIsHonoured();
        testLightResponseIsMonotonicAndCorrectlyOriented();
        testReaderOverPseudoTerminal();
        testConnectedButSilentTimeout();
        std::cout << "LDR tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "LDR tests failed: " << error.what() << "\n";
        return 1;
    }
}
