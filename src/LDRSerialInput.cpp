#include "LDRSerialInput.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

namespace uncanny {
namespace {

constexpr std::size_t kReadBufferSize = 4096;
constexpr int kReadPollTimeoutMs = 100;
constexpr int kReconnectDelayMs = 1000;

std::uint64_t monotonicMicroseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

}  // namespace

LDRSerialInput::LDRSerialInput(LDRSerialConfig config) : config_(std::move(config)) {
    if (config_.sensorMax <= config_.sensorMin) {
        throw std::invalid_argument("LDR sensorMax must be greater than sensorMin");
    }
    if (!(config_.filterAlpha > 0.0F && config_.filterAlpha <= 1.0F)) {
        throw std::invalid_argument("LDR filterAlpha must be in (0, 1]");
    }
    if (config_.baud <= 0) {
        throw std::invalid_argument("LDR baud must be positive");
    }
    if (config_.disconnectTimeoutUs == 0 || config_.minPupil < 0.0F ||
        config_.maxPupil < config_.minPupil || config_.maxPupil > 1.0F) {
        throw std::invalid_argument("invalid LDR response configuration");
    }
    if (config_.constrictionTime <= 0.0F || config_.dilationTime <= 0.0F ||
        config_.curve <= 0.0F || config_.disconnectTimeoutUs == 0) {
        throw std::invalid_argument("LDR response times, curve, and timeout must be positive");
    }
    sample_.port = config_.port;
}

LDRSerialInput::~LDRSerialInput() {
    stop();
}

void LDRSerialInput::start() {
    if (!config_.enabled || readerThread_.joinable()) {
        return;
    }
    stopRequested_ = false;
    readerThread_ = std::thread(&LDRSerialInput::readerLoop, this);
}

void LDRSerialInput::stop() {
    stopRequested_ = true;
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    closePort();
}

void LDRSerialInput::update() {
    if (!config_.enabled) {
        return;
    }

    const std::uint64_t nowUs = monotonicMicroseconds();
    const LDRSerialSample current = snapshot();
    const bool connected = readerConnected_.load(std::memory_order_acquire);
    if (!connected && current.connected && !current.timedOut) {
        const std::uint64_t elapsed = nowUs >= current.lastValidUpdateUs
                                          ? nowUs - current.lastValidUpdateUs
                                          : 0;
        if (elapsed > config_.disconnectTimeoutUs) {
            LDRSerialSample timedOut = current;
            timedOut.connected = false;
            timedOut.timedOut = true;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                sample_ = std::move(timedOut);
            }
        }
    }
}

LDRSerialSample LDRSerialInput::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sample_;
}

bool LDRSerialInput::isConnected() const {
    return readerConnected_.load(std::memory_order_acquire);
}

bool LDRSerialInput::isEnabled() const {
    return config_.enabled;
}

std::string LDRSerialInput::port() const {
    return config_.port;
}

bool LDRSerialInput::parseLine(const std::string& line, std::int64_t& value) {
    const std::string prefix = "LDR:";
    const bool hasPrefix = line.compare(0, prefix.size(), prefix) == 0;
    const std::size_t firstDigit = hasPrefix ? prefix.size() : 0;
    if (line.size() == firstDigit) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const std::int64_t parsed = std::strtoll(line.c_str() + firstDigit, &end, 10);
    if (errno != 0 || end == line.c_str() + firstDigit) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0' || parsed < 0) {
        return false;
    }
    value = parsed;
    return true;
}

float LDRSerialInput::normalize(std::int64_t raw, const LDRSerialConfig& config) {
    if (config.sensorMax <= config.sensorMin) {
        return 0.0F;
    }
    const long double range = static_cast<long double>(config.sensorMax - config.sensorMin);
    long double level = (static_cast<long double>(raw) - config.sensorMin) / range;
    level = std::max<long double>(0.0L, std::min<long double>(1.0L, level));
    if (config.invert) {
        level = 1.0L - level;
    }
    return clampUnit(static_cast<float>(level));
}

float LDRSerialInput::mapTarget(std::int64_t filtered, const LDRSerialConfig& config) {
    const float lightLevel = normalize(filtered, config);
    const float curveLevel = std::pow(lightLevel, std::max(0.0001F, config.curve));
    const float target = config.maxPupil +
                         (config.minPupil - config.maxPupil) * curveLevel;
    return clampUnit(target);
}

float LDRSerialInput::responseProgress(std::uint64_t elapsedUs, float responseTimeSeconds) {
    if (elapsedUs == 0 || responseTimeSeconds <= 0.0F) {
        return 0.0F;
    }
    const float elapsedSeconds = static_cast<float>(
        static_cast<long double>(elapsedUs) / 1'000'000.0L);
    const float tau = std::max(responseTimeSeconds / 4.0F, 0.001F);
    return 1.0F - std::exp(-elapsedSeconds / tau);
}

void LDRSerialInput::readerLoop() {
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (descriptor_ < 0 && !openPort()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
            continue;
        }

        if (!readAvailable()) {
            closePort();
            if (!config_.autoReconnect) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));
        }
    }
}

bool LDRSerialInput::openPort() {
    if (config_.port.empty() || descriptor_ >= 0) {
        return false;
    }

    const int descriptor = ::open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }

    termios attributes{};
    if (::tcgetattr(descriptor, &attributes) != 0) {
        ::close(descriptor);
        return false;
    }
    ::cfmakeraw(&attributes);
    speed_t speed = B115200;
    switch (config_.baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
#ifdef B230400
        case 230400: speed = B230400; break;
#endif
#ifdef B460800
        case 460800: speed = B460800; break;
#endif
#ifdef B921600
        case 921600: speed = B921600; break;
#endif
        default:
            ::close(descriptor);
            return false;
    }
    ::cfsetispeed(&attributes, speed);
    ::cfsetospeed(&attributes, speed);
    attributes.c_cflag |= CLOCAL | CREAD;
    attributes.c_cflag &= ~PARENB;
    attributes.c_cflag &= ~CSTOPB;
    attributes.c_cflag |= CS8;
    attributes.c_iflag &= ~(IXON | IXOFF | IXANY);
    attributes.c_oflag &= ~OPOST;
    attributes.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    if (::tcsetattr(descriptor, TCSANOW, &attributes) != 0) {
        ::close(descriptor);
        return false;
    }
    ::tcflush(descriptor, TCIFLUSH);

    descriptor_ = descriptor;
    readerConnected_.store(true, std::memory_order_release);
    publishConnected(true);
    return true;
}

void LDRSerialInput::closePort() {
    readerConnected_.store(false, std::memory_order_release);
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
    publishConnected(false);
}

bool LDRSerialInput::readAvailable() {
    pollfd descriptorInfo{descriptor_, POLLIN, 0};
    const int ready = ::poll(&descriptorInfo, 1, kReadPollTimeoutMs);
    if (ready < 0) {
        if (errno == EINTR) {
            return true;
        }
        return false;
    }
    if (ready == 0) {
        return true;
    }
    if ((descriptorInfo.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return false;
    }
    if ((descriptorInfo.revents & POLLIN) == 0) {
        return true;
    }

    std::array<char, kReadBufferSize> buffer{};
    const ssize_t bytesRead = ::read(descriptor_, buffer.data(), buffer.size());
    if (bytesRead < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    if (bytesRead == 0) {
        return false;
    }

    for (ssize_t index = 0; index < bytesRead; ++index) {
        const char character = buffer[static_cast<std::size_t>(index)];
        if (character == '\n') {
            std::int64_t value = 0;
            if (parseLine(pendingLine_, value)) {
                publishValue(value, monotonicMicroseconds());
            }
            pendingLine_.clear();
        } else if (character != '\r' && character != '\0') {
            pendingLine_.push_back(character);
            if (pendingLine_.size() > 64) {
                pendingLine_.clear();
            }
        }
    }
    return true;
}

void LDRSerialInput::publishConnected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    sample_.connected = connected;
    sample_.timedOut = false;
    sample_.port = config_.port;
}

void LDRSerialInput::publishValue(std::int64_t raw, std::uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool firstValue = sample_.lastValidUpdateUs == 0;
    const std::int64_t previous = firstValue ? raw : sample_.filtered;
    const float alpha = config_.filterAlpha;
    const std::int64_t filtered = static_cast<std::int64_t>(
        std::llround(static_cast<long double>(previous) * (1.0L - alpha) +
                     static_cast<long double>(raw) * alpha));
    sample_.connected = true;
    sample_.timedOut = false;
    sample_.raw = raw;
    sample_.filtered = filtered;
    sample_.normalized = normalize(filtered, config_);
    sample_.targetPupil = mapTarget(filtered, config_);
    sample_.lastUpdateUs = nowUs;
    sample_.lastValidUpdateUs = nowUs;
}

}  // namespace uncanny
