#pragma once

#include "core/MirrorTypes.h"

#include <atomic>
#include <cstdint>
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
        core::VideoCodec codec,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    bool push(core::EncodedVideoPacket packet);
    void flush();
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
    core::VideoCodec codec_ = core::VideoCodec::H264;
    std::uint64_t sourcePtsAnchor_ = 0;
    std::uint64_t runningPtsAnchor_ = 0;
    std::uint64_t lastVideoPts_ = 0;
    std::uint64_t initialLeadNs_ = 30 * 1'000'000ULL;
    bool videoTimelineInitialized_ = false;
};

} // namespace padmirror::media
