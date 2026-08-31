#pragma once

#include "core/IMediaIngress.h"
#include "media/AudioPipeline.h"
#include "media/MediaEngine.h"
#include "media/VideoPipeline.h"

#include <QVariantList>

#include <cstdint>
#include <functional>
#include <string>

namespace padmirror::app {
class Settings;
}

namespace padmirror::diagnostics {
class Metrics;
}

namespace padmirror::media {

class MediaSession final : public core::IMediaIngress {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    MediaSession();
    ~MediaSession();

    bool start(
        const app::Settings& settings,
        core::VideoCodec videoCodec,
        core::AudioCodec audioCodec,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    void setWindowHandle(std::uintptr_t windowHandle);
    [[nodiscard]] bool running() const;

    bool pushVideo(core::EncodedVideoPacket packet) override;
    bool pushAudio(core::EncodedAudioPacket packet) override;
    std::uint64_t signalDiscontinuity(core::DiscontinuityReason reason) override;
    void endOfStream() override;

    [[nodiscard]] std::uint64_t activeEpoch() const;
    [[nodiscard]] static bool hevcAvailable();
    static QVariantList audioDevices();

private:
    VideoPipeline videoPipeline_;
    AudioPipeline audioPipeline_;
    MediaEngine engine_;
};

} // namespace padmirror::media
