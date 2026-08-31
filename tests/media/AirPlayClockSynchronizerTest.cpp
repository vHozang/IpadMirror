#include "media/AirPlayClockSynchronizer.h"

#include <cassert>
#include <cstdint>

using padmirror::media::AirPlayClockSynchronizer;

int main() {
    constexpr std::uint64_t ms = 1'000'000;
    AirPlayClockSynchronizer clock;

    const auto audioFirst = clock.normalize(
        AirPlayClockSynchronizer::Stream::Audio, 0, 10 * ms, 5'000 * ms);
    assert(audioFirst.ptsNs >= 5'000 * ms);
    assert(audioFirst.ptsNs <= 5'020 * ms);

    const auto videoFirst = clock.normalize(
        AirPlayClockSynchronizer::Stream::Video, 0, 16 * ms, 5'090 * ms);
    assert(videoFirst.ptsNs >= 5'000 * ms);
    assert(videoFirst.ptsNs <= 5'110 * ms);

    const auto videoNext = clock.normalize(
        AirPlayClockSynchronizer::Stream::Video, 16 * ms, 16 * ms, 5'106 * ms);
    assert(videoNext.ptsNs > videoFirst.ptsNs);

    const auto resetAfterPause = clock.normalize(
        AirPlayClockSynchronizer::Stream::Video, 32 * ms, 16 * ms, 8'000 * ms);
    assert(resetAfterPause.rebased);
    assert(resetAfterPause.ptsNs >= 8'000 * ms);
    assert(resetAfterPause.ptsNs <= 8'020 * ms);

    clock.reset();
    const auto independentAudio = clock.normalize(
        AirPlayClockSynchronizer::Stream::Audio, 17'000'000 * ms, 10 * ms, 10'000 * ms);
    assert(independentAudio.ptsNs >= 10'000 * ms);
    assert(independentAudio.ptsNs <= 10'020 * ms);

    const auto independentVideo = clock.normalize(
        AirPlayClockSynchronizer::Stream::Video, 5'500'000 * ms, 16 * ms, 10'000 * ms);
    assert(independentVideo.ptsNs >= 10'000 * ms);
    assert(independentVideo.ptsNs <= 10'020 * ms);

    const auto independentVideoNext = clock.normalize(
        AirPlayClockSynchronizer::Stream::Video, 5'500'016 * ms, 16 * ms, 10'016 * ms);
    assert(independentVideoNext.ptsNs > independentVideo.ptsNs);
    assert(!independentVideoNext.rebased);
    return 0;
}
