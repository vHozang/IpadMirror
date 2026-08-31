#include "media/MediaEngine.h"

#include <utility>

namespace padmirror::media {

void MediaEngine::setSinks(VideoSink videoSink, AudioSink audioSink) {
    std::lock_guard lock(mutex_);
    videoSink_ = std::move(videoSink);
    audioSink_ = std::move(audioSink);
}

void MediaEngine::setFlushHandler(FlushHandler handler) {
    std::lock_guard lock(mutex_);
    flushHandler_ = std::move(handler);
}

void MediaEngine::setEndOfStreamHandler(EndOfStreamHandler handler) {
    std::lock_guard lock(mutex_);
    endOfStreamHandler_ = std::move(handler);
}

bool MediaEngine::pushVideo(core::EncodedVideoPacket packet) {
    const auto observation = epochController_.observeVideo(
        packet.epoch, packet.ptsNs, packet.format);
    if (!observation.accepted) return false;

    packet.epoch = observation.epoch;
    packet.discontinuity = packet.discontinuity || observation.discontinuity;
    if (packet.discontinuity) flush(observation.reason, packet.epoch);

    VideoSink sink;
    {
        std::lock_guard lock(mutex_);
        sink = videoSink_;
    }
    return sink && sink(std::move(packet));
}

bool MediaEngine::pushAudio(core::EncodedAudioPacket packet) {
    const auto observation = epochController_.observeAudio(
        packet.epoch, packet.ptsNs, packet.format);
    if (!observation.accepted) return false;

    packet.epoch = observation.epoch;
    packet.discontinuity = packet.discontinuity || observation.discontinuity;
    if (packet.discontinuity) flush(observation.reason, packet.epoch);

    AudioSink sink;
    {
        std::lock_guard lock(mutex_);
        sink = audioSink_;
    }
    return sink && sink(std::move(packet));
}

std::uint64_t MediaEngine::signalDiscontinuity(core::DiscontinuityReason reason) {
    const auto epoch = epochController_.signalDiscontinuity(reason);
    flush(reason, epoch);
    return epoch;
}

void MediaEngine::endOfStream() {
    EndOfStreamHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = endOfStreamHandler_;
    }
    if (handler) handler();
}

std::uint64_t MediaEngine::activeEpoch() const {
    return epochController_.activeEpoch();
}

core::DiscontinuityReason MediaEngine::lastDiscontinuityReason() const {
    return epochController_.lastReason();
}

void MediaEngine::flush(core::DiscontinuityReason reason, std::uint64_t epoch) {
    FlushHandler handler;
    {
        std::lock_guard lock(mutex_);
        handler = flushHandler_;
    }
    if (handler) handler(reason, epoch);
}

} // namespace padmirror::media
