#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace padmirror::capture {

enum class VideoCodec {
    H264,
    Hevc,
};

struct VideoFormat {
    VideoCodec codec = VideoCodec::H264;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
};

struct VideoPacket {
    std::uint64_t ptsNs = 0;
    std::vector<std::uint8_t> data;
    bool keyFrame = false;
    VideoFormat format;
};

enum class AudioCodec {
    PcmS16Le,
    AacEld,
    Unknown,
};

struct AudioFormat {
    AudioCodec codec = AudioCodec::PcmS16Le;
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
    std::uint32_t bitsPerChannel = 16;
};

struct AudioPacket {
    std::uint64_t ptsNs = 0;
    std::vector<std::uint8_t> data;
    AudioFormat format;
};

} // namespace padmirror::capture
