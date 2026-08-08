#pragma once

#include "capture/MediaPacket.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

typedef struct _GstElement GstElement;

namespace padmirror::app {
class Settings;
}

namespace padmirror::diagnostics {
class Metrics;
}

namespace padmirror::media {

class VideoPipeline {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    VideoPipeline() = default;
    ~VideoPipeline();

    bool start(
        const app::Settings& settings,
        capture::VideoCodec codec,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    bool push(capture::VideoPacket packet);
    void setWindowHandle(std::uintptr_t windowHandle);
    [[nodiscard]] bool running() const;

private:
    void monitorBus();

    mutable std::mutex mutex_;
    GstElement* pipeline_ = nullptr;
    GstElement* appSource_ = nullptr;
    GstElement* sink_ = nullptr;
    diagnostics::Metrics* metrics_ = nullptr;
    ErrorHandler errorHandler_;
    std::thread busThread_;
    std::atomic_bool busStopRequested_{false};
    std::uintptr_t windowHandle_ = 0;
    capture::VideoCodec codec_ = capture::VideoCodec::H264;
};

} // namespace padmirror::media
