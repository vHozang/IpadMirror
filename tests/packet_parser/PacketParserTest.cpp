#include "capture/usb/BinaryIO.h"
#include "capture/usb/PacketParser.h"

#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using padmirror::capture::usb::PacketParser;
using namespace padmirror::capture::usb::binary;

namespace {

std::vector<std::uint8_t> block(std::uint32_t magic, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> output;
    appendLe(output, static_cast<std::uint32_t>(payload.size() + 8));
    appendLe(output, magic);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::uint8_t> readFixture(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void validateReferenceFixtures(const std::filesystem::path& root) {
    PacketParser parser;
    std::string error;

    const auto videoFrame = readFixture(root / "asyn-feed");
    assert(videoFrame.size() >= 20);
    const auto video = parser.parseVideoSample(std::span(videoFrame).subspan(20), error);
    if (!error.empty()) std::cerr << "Reference video fixture: " << error << '\n';
    assert(error.empty());
    assert(video);
    assert(video->format.width != 0);
    assert(video->format.height != 0);
    assert(!video->format.sps.empty());
    assert(!video->format.pps.empty());
    assert(!video->data.empty());

    error.clear();
    const auto audioFrame = readFixture(root / "asyn-eat");
    assert(audioFrame.size() >= 16);
    const auto audioPayloadOffset = readLe<std::uint32_t>(audioFrame) == 0x6173796e ? 16U : 20U;
    const auto audio = parser.parseAudioSample(std::span(audioFrame).subspan(audioPayloadOffset), error);
    if (!error.empty()) std::cerr << "Reference audio fixture: " << error << '\n';
    assert(error.empty());
    assert(audio);
    assert(audio->format.codec == padmirror::capture::AudioCodec::PcmS16Le);
    assert(audio->format.sampleRate == 48000);
    assert(audio->format.channels == 2);
    assert(audio->data.size() == 4096);
}

} // namespace

int main() {
    std::vector<std::uint8_t> timePayload;
    appendLe(timePayload, std::uint64_t{30});
    appendLe(timePayload, std::uint32_t{60});
    appendLe(timePayload, std::uint32_t{0});
    appendLe(timePayload, std::uint64_t{0});
    const auto timeBlock = block(0x6f707473, timePayload);

    const std::vector<std::uint8_t> avcc = {
        0, 0, 0, 3,
        0x65, 0xaa, 0xbb,
    };
    const auto dataBlock = block(0x73646174, avcc);

    std::vector<std::uint8_t> rootPayload;
    rootPayload.insert(rootPayload.end(), timeBlock.begin(), timeBlock.end());
    rootPayload.insert(rootPayload.end(), dataBlock.begin(), dataBlock.end());
    const auto sampleBuffer = block(0x73627566, rootPayload);

    PacketParser parser;
    std::string error;
    const auto packet = parser.parseVideoSample(sampleBuffer, error);
    assert(error.empty());
    assert(packet.has_value());
    assert(packet->ptsNs == 500'000'000ULL);
    assert(packet->keyFrame);
    assert((packet->data == std::vector<std::uint8_t>{0, 0, 0, 1, 0x65, 0xaa, 0xbb}));

    if (const auto* fixtures = std::getenv("PADMIRROR_IOSCREEN_FIXTURES")) {
        validateReferenceFixtures(fixtures);
    }
    return 0;
}
