#include "media/MediaEpochController.h"

namespace padmirror::media {
namespace {

constexpr std::uint64_t kBackwardJumpNs = 1'000'000ULL;
constexpr std::uint64_t kForwardJumpNs = 250'000'000ULL;

bool isFormatSpecified(const core::VideoFormat& format) {
    return format.width > 0 || format.height > 0 || !format.sps.empty() || !format.pps.empty() || !format.vps.empty();
}

} // namespace

std::uint64_t MediaEpochController::activeEpoch() const {
    std::lock_guard lock(mutex_);
    return activeEpoch_;
}

core::DiscontinuityReason MediaEpochController::lastReason() const {
    std::lock_guard lock(mutex_);
    return lastReason_;
}

std::uint64_t MediaEpochController::signalDiscontinuity(core::DiscontinuityReason reason) {
    std::lock_guard lock(mutex_);
    return signalDiscontinuityLocked(reason);
}

MediaEpochController::Observation MediaEpochController::observeVideo(
    std::uint64_t ptsNs,
    const core::VideoFormat& format) {
    return observeVideo(0, ptsNs, format);
}

MediaEpochController::Observation MediaEpochController::observeVideo(
    std::uint64_t suppliedEpoch,
    std::uint64_t ptsNs,
    const core::VideoFormat& format) {
    std::lock_guard lock(mutex_);
    if (suppliedEpoch != 0 && suppliedEpoch < activeEpoch_) {
        return {activeEpoch_, false, false, lastReason_};
    }

    const bool specified = isFormatSpecified(format);
    const bool changed = specified && hasVideoFormat_ && videoFormat_ != format;
    if (specified) {
        videoFormat_ = format;
        hasVideoFormat_ = true;
    }
    return observe(
        ptsNs,
        hasVideoPts_,
        lastVideoPtsNs_,
        core::DiscontinuityReason::VideoFormatChanged,
        changed);
}

MediaEpochController::Observation MediaEpochController::observeAudio(
    std::uint64_t ptsNs,
    const core::AudioFormat& format) {
    return observeAudio(0, ptsNs, format);
}

MediaEpochController::Observation MediaEpochController::observeAudio(
    std::uint64_t suppliedEpoch,
    std::uint64_t ptsNs,
    const core::AudioFormat& format) {
    std::lock_guard lock(mutex_);
    if (suppliedEpoch != 0 && suppliedEpoch < activeEpoch_) {
        return {activeEpoch_, false, false, lastReason_};
    }

    const bool changed = hasAudioFormat_ && audioFormat_ != format;
    audioFormat_ = format;
    hasAudioFormat_ = true;
    return observe(
        ptsNs,
        hasAudioPts_,
        lastAudioPtsNs_,
        core::DiscontinuityReason::AudioFormatChanged,
        changed);
}

bool MediaEpochController::accepts(std::uint64_t epoch) const {
    std::lock_guard lock(mutex_);
    return epoch >= activeEpoch_;
}

void MediaEpochController::reset() {
    std::lock_guard lock(mutex_);
    hasVideoPts_ = false;
    hasAudioPts_ = false;
    lastVideoPtsNs_ = 0;
    lastAudioPtsNs_ = 0;
    hasVideoFormat_ = false;
    hasAudioFormat_ = false;
    videoFormat_ = {};
    audioFormat_ = {};
}

MediaEpochController::Observation MediaEpochController::observe(
    std::uint64_t ptsNs,
    bool& hasPts,
    std::uint64_t& lastPts,
    core::DiscontinuityReason formatReason,
    bool formatChanged) {
    if (formatChanged) {
        const auto epoch = signalDiscontinuityLocked(formatReason);
        hasPts = ptsNs != 0;
        lastPts = ptsNs;
        return {epoch, true, true, formatReason};
    }

    if (ptsNs != 0 && hasPts) {
        const bool backward = ptsNs + kBackwardJumpNs < lastPts;
        const bool forward = ptsNs > lastPts && ptsNs - lastPts > kForwardJumpNs;
        if (backward || forward) {
            const auto reason = backward
                ? core::DiscontinuityReason::TimestampReset
                : core::DiscontinuityReason::UserSeekLikeJump;
            const auto epoch = signalDiscontinuityLocked(reason);
            hasPts = ptsNs != 0;
            lastPts = ptsNs;
            return {epoch, true, true, reason};
        }
    }

    if (ptsNs != 0) {
        hasPts = true;
        lastPts = ptsNs;
    }
    return {activeEpoch_, true, false, lastReason_};
}

std::uint64_t MediaEpochController::signalDiscontinuityLocked(core::DiscontinuityReason reason) {
    ++activeEpoch_;
    lastReason_ = reason;
    hasVideoPts_ = false;
    hasAudioPts_ = false;
    lastVideoPtsNs_ = 0;
    lastAudioPtsNs_ = 0;
    return activeEpoch_;
}

} // namespace padmirror::media
