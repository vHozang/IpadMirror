#pragma once

#include "core/MirrorTypes.h"

#include <cstdint>
#include <mutex>

namespace padmirror::media {

class MediaEpochController {
public:
    struct Observation {
        std::uint64_t epoch = 0;
        bool accepted = true;
        bool discontinuity = false;
        core::DiscontinuityReason reason = core::DiscontinuityReason::Unknown;
    };

    [[nodiscard]] std::uint64_t activeEpoch() const;
    [[nodiscard]] core::DiscontinuityReason lastReason() const;
    [[nodiscard]] std::uint64_t signalDiscontinuity(core::DiscontinuityReason reason);

    [[nodiscard]] Observation observeVideo(
        std::uint64_t ptsNs,
        const core::VideoFormat& format);
    [[nodiscard]] Observation observeVideo(
        std::uint64_t suppliedEpoch,
        std::uint64_t ptsNs,
        const core::VideoFormat& format);
    [[nodiscard]] Observation observeAudio(
        std::uint64_t ptsNs,
        const core::AudioFormat& format);
    [[nodiscard]] Observation observeAudio(
        std::uint64_t suppliedEpoch,
        std::uint64_t ptsNs,
        const core::AudioFormat& format);

    [[nodiscard]] bool accepts(std::uint64_t epoch) const;
    void reset();

private:
    [[nodiscard]] Observation observe(
        std::uint64_t ptsNs,
        bool& hasPts,
        std::uint64_t& lastPts,
        core::DiscontinuityReason formatReason,
        bool formatChanged);
    [[nodiscard]] std::uint64_t signalDiscontinuityLocked(core::DiscontinuityReason reason);

    mutable std::mutex mutex_;
    std::uint64_t activeEpoch_ = 1;
    core::DiscontinuityReason lastReason_ = core::DiscontinuityReason::StreamRestart;
    bool hasVideoPts_ = false;
    bool hasAudioPts_ = false;
    std::uint64_t lastVideoPtsNs_ = 0;
    std::uint64_t lastAudioPtsNs_ = 0;
    bool hasVideoFormat_ = false;
    bool hasAudioFormat_ = false;
    core::VideoFormat videoFormat_;
    core::AudioFormat audioFormat_;
};

} // namespace padmirror::media
