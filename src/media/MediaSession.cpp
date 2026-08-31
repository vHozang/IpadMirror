#include "media/MediaSession.h"

#include "app/Settings.h"

#include <gst/gst.h>

#include <mutex>
#include <utility>

namespace padmirror::media {
namespace {

std::once_flag gstInitialization;

} // namespace

MediaSession::MediaSession() {
    std::call_once(gstInitialization, [] {
        gst_init(nullptr, nullptr);
    });
    engine_.setSinks(
        [this](core::EncodedVideoPacket packet) {
            return videoPipeline_.push(std::move(packet));
        },
        [this](core::EncodedAudioPacket packet) {
            return audioPipeline_.push(std::move(packet));
        });
    engine_.setFlushHandler([this](core::DiscontinuityReason reason, std::uint64_t) {
        // Video always flushes on a discontinuity: H.264/HEVC are reference-coded,
        // so after a source/app/format change the decoder must drop stale buffered
        // frames and wait for the next IDR.
        videoPipeline_.flush();
        // Audio is different. UxPlay flushes its audio pipeline ONLY on an actual
        // RTSP FLUSH or an audio-format change -- never on a timestamp jump from
        // switching YouTube videos, opening an app, or skipping. Flushing the
        // WASAPI sink on a mere time jump tears down its preroll and produces the
        // reported ~30 s of dead air before it recovers. Keep the audio pipeline
        // running across time-based discontinuities; the single-anchor PTS map in
        // AudioPipeline::push re-aligns the next buffer to the live edge with no
        // gap. Flush audio only when the stream itself is genuinely reset.
        switch (reason) {
        case core::DiscontinuityReason::AudioFormatChanged:
        case core::DiscontinuityReason::StreamRestart:
        case core::DiscontinuityReason::TransportSwitch:
        case core::DiscontinuityReason::TransportReconnect:
        case core::DiscontinuityReason::DecoderReset:
            audioPipeline_.flush();
            break;
        default:
            break;
        }
    });
}

MediaSession::~MediaSession() {
    stop();
}

bool MediaSession::start(
    const app::Settings& settings,
    core::VideoCodec videoCodec,
    core::AudioCodec audioCodec,
    std::uintptr_t windowHandle,
    diagnostics::Metrics* metrics,
    ErrorHandler errorHandler) {
    stop();
    if (!videoPipeline_.start(settings, videoCodec, windowHandle, metrics, errorHandler)) {
        return false;
    }
    if (!audioPipeline_.start(settings, audioCodec, metrics, std::move(errorHandler))) {
        videoPipeline_.stop();
        return false;
    }
    signalDiscontinuity(core::DiscontinuityReason::StreamRestart);
    return true;
}

void MediaSession::stop() {
    audioPipeline_.stop();
    videoPipeline_.stop();
}

void MediaSession::setWindowHandle(std::uintptr_t windowHandle) {
    videoPipeline_.setWindowHandle(windowHandle);
}

bool MediaSession::running() const {
    return videoPipeline_.running() && audioPipeline_.running();
}

bool MediaSession::pushVideo(core::EncodedVideoPacket packet) {
    return engine_.pushVideo(std::move(packet));
}

bool MediaSession::pushAudio(core::EncodedAudioPacket packet) {
    return engine_.pushAudio(std::move(packet));
}

std::uint64_t MediaSession::signalDiscontinuity(core::DiscontinuityReason reason) {
    return engine_.signalDiscontinuity(reason);
}

void MediaSession::endOfStream() {
    engine_.endOfStream();
}

std::uint64_t MediaSession::activeEpoch() const {
    return engine_.activeEpoch();
}

bool MediaSession::hevcAvailable() {
    auto* parser = gst_element_factory_find("h265parse");
    if (!parser) return false;
    gst_object_unref(parser);
    auto* decoder = gst_element_factory_find("d3d11h265dec");
    if (!decoder) return false;
    gst_object_unref(decoder);
    return true;
}

QVariantList MediaSession::audioDevices() {
    return AudioPipeline::enumerateOutputDevices();
}

} // namespace padmirror::media
