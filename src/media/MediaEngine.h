#pragma once

#include "core/IMediaIngress.h"
#include "media/MediaEpochController.h"

#include <functional>
#include <mutex>

namespace padmirror::media {

class MediaEngine final : public core::IMediaIngress {
public:
    using VideoSink = std::function<bool(core::EncodedVideoPacket)>;
    using AudioSink = std::function<bool(core::EncodedAudioPacket)>;
    using FlushHandler = std::function<void(core::DiscontinuityReason, std::uint64_t)>;
    using EndOfStreamHandler = std::function<void()>;

    void setSinks(VideoSink videoSink, AudioSink audioSink);
    void setFlushHandler(FlushHandler handler);
    void setEndOfStreamHandler(EndOfStreamHandler handler);

    bool pushVideo(core::EncodedVideoPacket packet) override;
    bool pushAudio(core::EncodedAudioPacket packet) override;
    std::uint64_t signalDiscontinuity(core::DiscontinuityReason reason) override;
    void endOfStream() override;

    [[nodiscard]] std::uint64_t activeEpoch() const;
    [[nodiscard]] core::DiscontinuityReason lastDiscontinuityReason() const;

private:
    void flush(core::DiscontinuityReason reason, std::uint64_t epoch);

    mutable std::mutex mutex_;
    MediaEpochController epochController_;
    VideoSink videoSink_;
    AudioSink audioSink_;
    FlushHandler flushHandler_;
    EndOfStreamHandler endOfStreamHandler_;
};

} // namespace padmirror::media
