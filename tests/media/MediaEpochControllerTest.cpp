#include "media/MediaEpochController.h"

#include <cassert>

using padmirror::core::AudioFormat;
using padmirror::core::DiscontinuityReason;
using padmirror::core::VideoFormat;
using padmirror::media::MediaEpochController;

int main() {
    MediaEpochController controller;
    const auto initialEpoch = controller.activeEpoch();

    const auto firstVideo = controller.observeVideo(100'000'000ULL, VideoFormat{});
    assert(firstVideo.epoch == initialEpoch);
    assert(!firstVideo.discontinuity);

    const auto steadyVideo = controller.observeVideo(115'000'000ULL, VideoFormat{});
    assert(steadyVideo.epoch == initialEpoch);
    assert(!steadyVideo.discontinuity);

    const auto backward = controller.observeVideo(113'000'000ULL, VideoFormat{});
    assert(backward.discontinuity);
    assert(backward.reason == DiscontinuityReason::TimestampReset);
    assert(backward.epoch == initialEpoch + 1);

    const auto forward = controller.observeVideo(500'000'000ULL, VideoFormat{});
    assert(forward.discontinuity);
    assert(forward.reason == DiscontinuityReason::UserSeekLikeJump);
    assert(forward.epoch == initialEpoch + 2);

    VideoFormat resized;
    resized.width = 1920;
    resized.height = 1080;
    const auto initialFormat = controller.observeVideo(510'000'000ULL, resized);
    assert(!initialFormat.discontinuity);
    resized.width = 1080;
    resized.height = 1920;
    const auto formatChange = controller.observeVideo(525'000'000ULL, resized);
    assert(formatChange.discontinuity);
    assert(formatChange.reason == DiscontinuityReason::VideoFormatChanged);

    const auto firstAudio = controller.observeAudio(100'000'000ULL, AudioFormat{});
    assert(!firstAudio.discontinuity);
    AudioFormat changedAudio;
    changedAudio.channels = 1;
    const auto audioChange = controller.observeAudio(110'000'000ULL, changedAudio);
    assert(audioChange.discontinuity);
    assert(audioChange.reason == DiscontinuityReason::AudioFormatChanged);

    const auto explicitEpoch = controller.signalDiscontinuity(DiscontinuityReason::TransportSwitch);
    assert(controller.activeEpoch() == explicitEpoch);
    assert(controller.lastReason() == DiscontinuityReason::TransportSwitch);
    assert(!controller.accepts(explicitEpoch - 1));
    assert(controller.accepts(explicitEpoch));
    return 0;
}
