#include "media/MediaSession.h"

#include "app/Settings.h"

#include <gst/gst.h>

#include <mutex>

namespace padmirror::media {
namespace {
std::once_flag gstInitialization;
}

MediaSession::MediaSession() {
    std::call_once(gstInitialization, [] {
        gst_init(nullptr, nullptr);
    });
}

MediaSession::~MediaSession() {
    stop();
}

bool MediaSession::startUsb(
    const app::Settings& settings,
    capture::VideoCodec videoCodec,
    capture::AudioCodec audioCodec,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    if (!videoPipeline_.start(settings, videoCodec, windowHandle, metrics, errorHandler)) return false;
    if (!audioPipeline_.start(settings, audioCodec, metrics, errorHandler)) {
        videoPipeline_.stop();
        return false;
    }
    mode_.store(Mode::Usb);
    return true;
}

bool MediaSession::startLan(
    const app::Settings& settings,
    std::uint16_t videoPort,
    std::uint16_t audioPort,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    if (!lanPipeline_.start(
            settings, videoPort, audioPort, windowHandle, metrics, std::move(errorHandler))) {
        return false;
    }
    mode_.store(Mode::Lan);
    return true;
}

void MediaSession::stop() {
    mode_.store(Mode::Stopped);
    videoPipeline_.stop();
    audioPipeline_.stop();
    lanPipeline_.stop();
}

void MediaSession::pushVideo(capture::VideoPacket packet) {
    if (mode_.load() == Mode::Usb) videoPipeline_.push(std::move(packet));
}

void MediaSession::pushAudio(capture::AudioPacket packet) {
    if (mode_.load() == Mode::Usb) audioPipeline_.push(std::move(packet));
}

void MediaSession::setWindowHandle(std::uintptr_t windowHandle) {
    videoPipeline_.setWindowHandle(windowHandle);
    lanPipeline_.setWindowHandle(windowHandle);
}

MediaSession::Mode MediaSession::mode() const {
    return mode_.load();
}

QVariantList MediaSession::audioDevices() {
    return AudioPipeline::enumerateOutputDevices();
}

} // namespace padmirror::media
