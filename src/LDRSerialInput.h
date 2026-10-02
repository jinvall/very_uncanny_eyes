#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace uncanny {

struct LDRSerialConfig {
    bool enabled = false;
    std::string port;
    int baud = 115200;
    int sensorMin = 0;
    int sensorMax = 4095;
    bool invert = false;
    float filterAlpha = 0.15F;
    float minPupil = 0.25F;
    float maxPupil = 1.0F;
    float curve = 0.45F;
    float constrictionTime = 0.35F;
    float dilationTime = 0.80F;
    std::uint64_t disconnectTimeoutUs = 2'000'000;
    bool autoReconnect = true;
    float irisWeight = 0.75F;
};

struct LDRSerialSample {
    bool connected = false;
    bool timedOut = false;
    std::string port;
    std::int64_t raw = 0;
    std::int64_t filtered = 0;
    float normalized = 0.0F;
    float targetPupil = 0.0F;
    std::uint64_t lastUpdateUs = 0;
    std::uint64_t lastValidUpdateUs = 0;
};

class LDRSerialInput {
public:
    explicit LDRSerialInput(LDRSerialConfig config = {});
    ~LDRSerialInput();

    LDRSerialInput(const LDRSerialInput&) = delete;
    LDRSerialInput& operator=(const LDRSerialInput&) = delete;

    void start();
    void stop();
    void update();
    LDRSerialSample snapshot() const;
    bool isConnected() const;
    bool isEnabled() const;
    std::string port() const;

    static bool parseLine(const std::string& line, std::int64_t& value);
    static float normalize(std::int64_t raw, const LDRSerialConfig& config);
    static float mapTarget(std::int64_t filtered, const LDRSerialConfig& config);
    static float responseProgress(std::uint64_t elapsedUs, float responseTimeSeconds);

private:
    void readerLoop();
    bool openPort();
    void closePort();
    bool readAvailable();
    void publishConnected(bool connected);
    void publishValue(std::int64_t raw, std::uint64_t nowUs);

    LDRSerialConfig config_;
    std::thread readerThread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> readerConnected_{false};
    int descriptor_ = -1;
    mutable std::mutex mutex_;
    LDRSerialSample sample_;
    std::string pendingLine_;
};

}  // namespace uncanny
