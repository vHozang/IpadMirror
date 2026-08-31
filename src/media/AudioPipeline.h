#pragma once

#include "core/MirrorTypes.h"
#include "media/AudioRingBuffer.h"
#include "media/FdkAacEldDecoder.h"

#include <QVariantList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
        core::AudioCodec codec,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    bool push(core::EncodedAudioPacket packet);
    void flush();
    [[nodiscard]] bool running() const;

    static QVariantList enumerateOutputDevices();

private:
    void flushLocked();
    void feed();
    void monitorBus();

    mutable std::mutex mutex_;
    std::shared_ptr<AudioRingBuffer> ringBuffer_;
    GstElement* pipeline_ = nullptr;
    GstElement* appSource_ = nullptr;
    diagnostics::Metrics* metrics_ = nullptr;
    ErrorHandler errorHandler_;
    std::thread feederThread_;
    std::thread busThread_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool resyncRequested_{false};
    std::uint64_t nextPtsNs_ = 0;
    std::uint64_t sourcePtsAnchor_ = 0;
    std::uint64_t runningPtsAnchor_ = 0;
    std::uint64_t initialLeadNs_ = 10 * 1'000'000ULL;
    bool ptsInitialized_ = false;
    bool inputTimelineInitialized_ = false;
    std::atomic_bool unsupportedAudioReported_{false};
    core::AudioCodec codec_ = core::AudioCodec::PcmS16Le;
    // AAC-ELD (AirPlay) is decoded to PCM in-process via libfdk-aac
    // because avdec_aac cannot decode AOT-39; null on the PCM/USB path.
    std::unique_ptr<FdkAacEldDecoder> eldDecoder_;
    std::vector<std::int16_t> eldPcm_;
    // AAC-ELD (AirPlay) PTS mapping follows UxPlay: a single per-session anchor
    // maps the NTP source time onto pipeline running-time, and every later frame
    // is anchorRunning + (sourcePts - anchorSource). No burst governor and no
    // gapless re-laying -- real gaps are carried in the timestamp so audio stays
    // at the live edge on its own, exactly as UxPlay does. sourcePtsAnchor_/
    // runningPtsAnchor_/nextPtsNs_/inputTimelineInitialized_ (declared above)
    // hold that anchor for both the PCM and AAC-ELD paths.
};

} // namespace padmirror::media
