#pragma once

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

class LanPipeline {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    LanPipeline() = default;
    ~LanPipeline();

    bool start(
        const app::Settings& settings,
        std::uint16_t videoPort,
        std::uint16_t audioPort,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    void setWindowHandle(std::uintptr_t windowHandle);
    [[nodiscard]] bool running() const;

private:
    void monitorBus();

    mutable std::mutex mutex_;
    GstElement* pipeline_ = nullptr;
    GstElement* videoSink_ = nullptr;
    diagnostics::Metrics* metrics_ = nullptr;
    ErrorHandler errorHandler_;
    std::thread busThread_;
    std::atomic_bool busStopRequested_{false};
    std::uintptr_t windowHandle_ = 0;
};

} // namespace padmirror::media
