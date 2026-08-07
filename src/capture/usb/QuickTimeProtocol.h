#pragma once

#include "capture/MediaPacket.h"
#include "capture/usb/PacketParser.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace padmirror::capture::usb {

class QuickTimeProtocol {
public:
    using Writer = std::function<bool(std::span<const std::uint8_t>)>;
    using VideoHandler = std::function<void(VideoPacket)>;
    using AudioHandler = std::function<void(AudioPacket)>;
    using MessageHandler = std::function<void(const std::string&)>;
    using ReadyHandler = std::function<void()>;

    void setWriter(Writer writer);
    void setVideoHandler(VideoHandler handler);
    void setAudioHandler(AudioHandler handler);
    void setErrorHandler(MessageHandler handler);
    void setReadyHandler(ReadyHandler handler);

    bool processFrame(std::span<const std::uint8_t> frame);

    void beginClose();
    bool waitForRelease(std::chrono::milliseconds timeout);
    void finishClose();
    void reset();

private:
    bool processSync(std::span<const std::uint8_t> frame);
    bool processAsync(std::span<const std::uint8_t> frame);
    bool send(const std::vector<std::uint8_t>& packet);
    void reportError(const std::string& message) const;
    void updateAudioClock(const AudioPacket& packet);
    double currentSkew() const;

    Writer writer_;
    VideoHandler videoHandler_;
    AudioHandler audioHandler_;
    MessageHandler errorHandler_;
    ReadyHandler readyHandler_;
    PacketParser parser_;

    std::vector<std::uint8_t> needPacket_;
    std::uint64_t deviceAudioClockRef_ = 0;
    std::chrono::steady_clock::time_point hostClockStart_{};
    std::chrono::steady_clock::time_point audioClockStart_{};
    bool hostClockRunning_ = false;
    bool audioClockRunning_ = false;
    bool hasAudioTiming_ = false;
    std::uint32_t audioSampleRate_ = 48000;
    std::uint64_t firstAudioDeviceNs_ = 0;
    std::uint64_t lastAudioDeviceNs_ = 0;
    std::uint64_t firstAudioHostNs_ = 0;
    std::uint64_t lastAudioHostNs_ = 0;

    mutable std::mutex releaseMutex_;
    std::condition_variable releaseCondition_;
    unsigned releaseCount_ = 0;
};

} // namespace padmirror::capture::usb
