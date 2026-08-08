#pragma once

#include "capture/MediaPacket.h"
#include "media/AudioRingBuffer.h"

#include <QVariantList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

class AudioPipeline {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    AudioPipeline() = default;
    ~AudioPipeline();

    bool start(
        const app::Settings& settings,
        capture::AudioCodec codec,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    bool push(capture::AudioPacket packet);
    [[nodiscard]] bool running() const;

    static QVariantList enumerateOutputDevices();

private:
    void feed();
    void monitorBus();

    mutable std::mutex mutex_;
    std::unique_ptr<AudioRingBuffer> ringBuffer_;
    GstElement* pipeline_ = nullptr;
    GstElement* appSource_ = nullptr;
    diagnostics::Metrics* metrics_ = nullptr;
    ErrorHandler errorHandler_;
    std::thread feederThread_;
    std::thread busThread_;
    std::atomic_bool stopRequested_{false};
    std::uint64_t nextPtsNs_ = 0;
    bool ptsInitialized_ = false;
    bool strictSync_ = false;
    std::atomic_bool unsupportedAudioReported_{false};
    capture::AudioCodec codec_ = capture::AudioCodec::PcmS16Le;
};

} // namespace padmirror::media
