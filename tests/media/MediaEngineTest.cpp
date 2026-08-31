#include "media/MediaEngine.h"

#include <cassert>
#include <memory>
#include <vector>

using padmirror::core::DiscontinuityReason;
using padmirror::core::EncodedAudioPacket;
using padmirror::core::EncodedVideoPacket;
using padmirror::media::MediaEngine;

int main() {
    MediaEngine engine;
    unsigned videoCount = 0;
    unsigned audioCount = 0;
    unsigned flushCount = 0;
    unsigned eosCount = 0;
    std::uint64_t lastFlushedEpoch = 0;

    engine.setSinks(
        [&](EncodedVideoPacket packet) {
            ++videoCount;
            assert(packet.payload && !packet.payload->empty());
            return true;
        },
        [&](EncodedAudioPacket packet) {
            ++audioCount;
            assert(packet.payload && !packet.payload->empty());
            return true;
        });
    engine.setFlushHandler([&](DiscontinuityReason, std::uint64_t epoch) {
        ++flushCount;
        lastFlushedEpoch = epoch;
    });
    engine.setEndOfStreamHandler([&] { ++eosCount; });

    const auto payload = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{0, 1, 2, 3});

    EncodedVideoPacket video;
    video.payload = payload;
    video.ptsNs = 100'000'000ULL;
    assert(engine.pushVideo(video));
    assert(videoCount == 1);

    EncodedAudioPacket audio;
    audio.payload = payload;
    audio.ptsNs = 100'000'000ULL;
    assert(engine.pushAudio(audio));
    assert(audioCount == 1);

    video.ptsNs = 500'000'000ULL;
    assert(engine.pushVideo(video));
    assert(flushCount == 1);
    assert(lastFlushedEpoch == engine.activeEpoch());

    video.epoch = lastFlushedEpoch - 1;
    video.ptsNs = 510'000'000ULL;
    assert(!engine.pushVideo(video));
    assert(videoCount == 2);
    assert(engine.activeEpoch() == lastFlushedEpoch);

    engine.signalDiscontinuity(DiscontinuityReason::TransportSwitch);
    assert(flushCount == 2);
    engine.endOfStream();
    assert(eosCount == 1);
    return 0;
}
