#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace padmirror::core {

enum class MirrorTransport {
    None,
    Usb,
    AirPlay,
};

enum class MirrorState {
    Stopped,
    Waiting,
    Connecting,
    Streaming,
    Recovering,
    Error,
};

enum class DiscontinuityReason {
    Unknown,
    UserSeekLikeJump,
    VideoSourceChanged,
    AppSwitch,
    StreamRestart,
    TimestampReset,
    VideoFormatChanged,
    AudioFormatChanged,
    TransportReconnect,
    OrientationChanged,
    DecoderReset,
    TransportSwitch,
};

enum class VideoCodec {
    H264,
    Hevc,
};

enum class AudioCodec {
    PcmS16Le,
    AacEld,
    Unknown,
};

using SharedBuffer = std::shared_ptr<const std::vector<std::uint8_t>>;

struct VideoFormat {
    VideoCodec codec = VideoCodec::H264;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
    std::vector<std::uint8_t> vps;

    [[nodiscard]] bool operator==(const VideoFormat&) const = default;
};

struct AudioFormat {
    AudioCodec codec = AudioCodec::PcmS16Le;
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
    std::uint32_t bitsPerChannel = 16;

    [[nodiscard]] bool operator==(const AudioFormat&) const = default;
};

struct EncodedVideoPacket {
    VideoCodec codec = VideoCodec::H264;
    SharedBuffer payload;
    std::uint64_t ptsNs = 0;
    std::uint64_t epoch = 0;
    bool keyframe = false;
    bool discontinuity = false;
    VideoFormat format;
};

struct EncodedAudioPacket {
    AudioCodec codec = AudioCodec::PcmS16Le;
    SharedBuffer payload;
    std::uint64_t ptsNs = 0;
    std::uint64_t epoch = 0;
    bool discontinuity = false;
    AudioFormat format;
};

struct MirrorHealth {
    bool available = false;
    bool trusted = false;
    std::string detail;
};

} // namespace padmirror::core
