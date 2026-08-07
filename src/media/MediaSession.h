#pragma once

#include "capture/MediaPacket.h"
#include "media/AudioPipeline.h"
#include "media/LanPipeline.h"
#include "media/VideoPipeline.h"

#include <QVariantList>

#include <atomic>
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

class MediaSession {
public:
    enum class Mode {
        Stopped,
        Usb,
        Lan,
    };

    using ErrorHandler = std::function<void(const std::string&)>;

    MediaSession();
    ~MediaSession();

    bool startUsb(
        const app::Settings& settings,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    bool startLan(
        const app::Settings& settings,
        std::uint16_t videoPort,
        std::uint16_t audioPort,
        std::uintptr_t windowHandle,
        diagnostics::Metrics* metrics,
        ErrorHandler errorHandler);
    void stop();
    void pushVideo(capture::VideoPacket packet);
    void pushAudio(capture::AudioPacket packet);
    void setWindowHandle(std::uintptr_t windowHandle);

    [[nodiscard]] Mode mode() const;
    static QVariantList audioDevices();

private:
    VideoPipeline videoPipeline_;
    AudioPipeline audioPipeline_;
    LanPipeline lanPipeline_;
    std::atomic<Mode> mode_{Mode::Stopped};
};

} // namespace padmirror::media
