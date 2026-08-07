#pragma once

#include "capture/MediaPacket.h"

#include <optional>
#include <span>
#include <string>

namespace padmirror::capture::usb {

class PacketParser {
public:
    std::optional<VideoPacket> parseVideoSample(
        std::span<const std::uint8_t> sampleBuffer,
        std::string& error);
    std::optional<AudioPacket> parseAudioSample(
        std::span<const std::uint8_t> sampleBuffer,
        std::string& error);

    void reset();

private:
    VideoFormat videoFormat_;
    AudioFormat audioFormat_;
};

} // namespace padmirror::capture::usb
