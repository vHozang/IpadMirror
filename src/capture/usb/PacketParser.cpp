#include "capture/usb/PacketParser.h"

#include "capture/usb/BinaryIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace padmirror::capture::usb {
namespace {

using namespace binary;

constexpr std::uint32_t kSampleBuffer = 0x73627566;
constexpr std::uint32_t kOutputPts = 0x6f707473;
constexpr std::uint32_t kSampleData = 0x73646174;
constexpr std::uint32_t kFormatDescription = 0x66647363;
constexpr std::uint32_t kMediaType = 0x6d646961;
constexpr std::uint32_t kMediaVideo = 0x76696465;
constexpr std::uint32_t kMediaSound = 0x736f756e;
constexpr std::uint32_t kVideoDimensions = 0x7664696d;
constexpr std::uint32_t kCodec = 0x636f6463;
constexpr std::uint32_t kAvc1 = 0x61766331;
constexpr std::uint32_t kExtensions = 0x6578746e;
constexpr std::uint32_t kAudioDescription = 0x61736264;
constexpr std::uint32_t kKeyValue = 0x6b657976;
constexpr std::uint32_t kIndexKey = 0x6964786b;
constexpr std::uint32_t kDictionary = 0x64696374;
constexpr std::uint32_t kDataValue = 0x64617476;
constexpr std::uint32_t kLinearPcm = 0x6c70636d;

struct ParsedSample {
    std::uint64_t ptsNs = 0;
    std::vector<std::uint8_t> data;
    std::optional<VideoFormat> videoFormat;
    std::optional<AudioFormat> audioFormat;
};

std::uint64_t parseTimeNs(std::span<const std::uint8_t> block) {
    if (block.size() < 32) {
        throw std::runtime_error("truncated CMTime block");
    }
    const auto value = readLe<std::int64_t>(block, 8);
    const auto scale = readLe<std::uint32_t>(block, 16);
    if (value <= 0 || scale == 0) {
        return 0;
    }

    const long double nanoseconds =
        static_cast<long double>(value) * 1'000'000'000.0L / static_cast<long double>(scale);
    if (nanoseconds >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return 0;
    }
    return static_cast<std::uint64_t>(nanoseconds);
}

std::optional<std::span<const std::uint8_t>> findIndexedValue(
    std::span<const std::uint8_t> dictionary,
    std::uint16_t wantedKey) {
    if (dictionary.size() < 8) {
        return std::nullopt;
    }
    const auto dictionaryLength = readLe<std::uint32_t>(dictionary);
    if (dictionaryLength < 8 || dictionaryLength > dictionary.size()) {
        return std::nullopt;
    }

    std::size_t offset = 8;
    while (offset < dictionaryLength) {
        const auto pair = checkedBlock(dictionary.first(dictionaryLength), offset, kKeyValue);
        if (pair.size() < 18) {
            return std::nullopt;
        }
        const auto keyBlock = checkedBlock(pair, 8, kIndexKey);
        if (keyBlock.size() < 10) {
            return std::nullopt;
        }
        const auto key = readLe<std::uint16_t>(keyBlock, 8);
        const auto valueOffset = 8 + keyBlock.size();
        if (valueOffset + 8 > pair.size()) {
            return std::nullopt;
        }
        const auto valueLength = readLe<std::uint32_t>(pair, valueOffset);
        if (valueLength < 8 || valueOffset + valueLength > pair.size()) {
            return std::nullopt;
        }
        if (key == wantedKey) {
            return pair.subspan(valueOffset, valueLength);
        }
        offset += pair.size();
    }
    return std::nullopt;
}

bool parseAvcConfiguration(
    std::span<const std::uint8_t> bytes,
    std::vector<std::uint8_t>& sps,
    std::vector<std::uint8_t>& pps) {
    if (bytes.size() < 7 || bytes[0] != 1) {
        return false;
    }

    std::size_t offset = 5;
    const auto spsCount = static_cast<std::size_t>(bytes[offset++] & 0x1fU);
    for (std::size_t index = 0; index < spsCount; ++index) {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const auto length = readBe<std::uint16_t>(bytes, offset);
        offset += 2;
        if (offset + length > bytes.size()) {
            return false;
        }
        if (sps.empty()) {
            sps.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
        offset += length;
    }

    if (offset >= bytes.size()) {
        return false;
    }
    const auto ppsCount = static_cast<std::size_t>(bytes[offset++]);
    for (std::size_t index = 0; index < ppsCount; ++index) {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const auto length = readBe<std::uint16_t>(bytes, offset);
        offset += 2;
        if (offset + length > bytes.size()) {
            return false;
        }
        if (pps.empty()) {
            pps.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
        offset += length;
    }
    return !sps.empty() && !pps.empty();
}

VideoFormat parseVideoFormat(std::span<const std::uint8_t> block) {
    checkedBlock(block, 0, kFormatDescription);
    VideoFormat format;
    std::size_t offset = 8;

    const auto media = checkedBlock(block, offset, kMediaType);
    if (media.size() != 12 || readLe<std::uint32_t>(media, 8) != kMediaVideo) {
        throw std::runtime_error("format description is not video");
    }
    offset += media.size();

    const auto dimensions = checkedBlock(block, offset, kVideoDimensions);
    if (dimensions.size() != 16) {
        throw std::runtime_error("invalid video dimension block");
    }
    format.width = readLe<std::uint32_t>(dimensions, 8);
    format.height = readLe<std::uint32_t>(dimensions, 12);
    offset += dimensions.size();

    const auto codec = checkedBlock(block, offset, kCodec);
    if (codec.size() != 12 || readLe<std::uint32_t>(codec, 8) != kAvc1) {
        throw std::runtime_error("USB stream is not H.264/AVC");
    }
    offset += codec.size();

    const auto extensions = checkedBlock(block, offset, kExtensions);
    const auto levelOne = findIndexedValue(extensions, 49);
    if (!levelOne || readLe<std::uint32_t>(*levelOne, 4) != kDictionary) {
        return format;
    }
    const auto avcValue = findIndexedValue(*levelOne, 105);
    if (!avcValue || readLe<std::uint32_t>(*avcValue, 4) != kDataValue) {
        return format;
    }
    parseAvcConfiguration(avcValue->subspan(8), format.sps, format.pps);
    return format;
}

AudioFormat parseAudioFormat(std::span<const std::uint8_t> block) {
    checkedBlock(block, 0, kFormatDescription);
    std::size_t offset = 8;

    const auto media = checkedBlock(block, offset, kMediaType);
    if (media.size() != 12 || readLe<std::uint32_t>(media, 8) != kMediaSound) {
        throw std::runtime_error("format description is not audio");
    }
    offset += media.size();

    const auto description = checkedBlock(block, offset, kAudioDescription);
    if (description.size() < 48) {
        throw std::runtime_error("truncated audio stream description");
    }

    AudioFormat format;
    const auto rateBits = readLe<std::uint64_t>(description, 8);
    const auto rate = std::bit_cast<double>(rateBits);
    const auto codec = readLe<std::uint32_t>(description, 16);
    format.codec = codec == kLinearPcm ? AudioCodec::PcmS16Le : AudioCodec::Unknown;
    format.sampleRate = std::isfinite(rate) && rate > 0.0 ? static_cast<std::uint32_t>(std::llround(rate)) : 48000;
    format.channels = readLe<std::uint32_t>(description, 36);
    format.bitsPerChannel = readLe<std::uint32_t>(description, 40);
    return format;
}

ParsedSample parseSample(std::span<const std::uint8_t> sampleBuffer, bool video) {
    const auto root = checkedBlock(sampleBuffer, 0, kSampleBuffer);
    ParsedSample parsed;
    std::size_t offset = 8;

    while (offset < root.size()) {
        const auto block = checkedBlock(root, offset);
        const auto magic = readLe<std::uint32_t>(block, 4);
        if (magic == kOutputPts) {
            parsed.ptsNs = parseTimeNs(block);
        } else if (magic == kSampleData) {
            parsed.data.assign(block.begin() + 8, block.end());
        } else if (magic == kFormatDescription) {
            if (video) {
                parsed.videoFormat = parseVideoFormat(block);
            } else {
                parsed.audioFormat = parseAudioFormat(block);
            }
        }
        offset += block.size();
    }
    return parsed;
}

void appendStartCode(std::vector<std::uint8_t>& output) {
    output.insert(output.end(), {0, 0, 0, 1});
}

void appendNalu(std::vector<std::uint8_t>& output, std::span<const std::uint8_t> nalu) {
    appendStartCode(output);
    output.insert(output.end(), nalu.begin(), nalu.end());
}

bool convertAvccToAnnexB(
    std::span<const std::uint8_t> input,
    std::vector<std::uint8_t>& output,
    bool& keyFrame) {
    if (input.size() >= 4 && input[0] == 0 && input[1] == 0 &&
        ((input[2] == 1) || (input[2] == 0 && input[3] == 1))) {
        output.insert(output.end(), input.begin(), input.end());
        for (std::size_t index = 0; index + 4 < input.size(); ++index) {
            if (input[index] == 0 && input[index + 1] == 0 &&
                ((input[index + 2] == 1 && (input[index + 3] & 0x1fU) == 5) ||
                 (input[index + 2] == 0 && input[index + 3] == 1 &&
                  (input[index + 4] & 0x1fU) == 5))) {
                keyFrame = true;
            }
        }
        return true;
    }

    std::size_t offset = 0;
    while (offset < input.size()) {
        if (offset + 4 > input.size()) {
            return false;
        }
        const auto length = readBe<std::uint32_t>(input, offset);
        offset += 4;
        if (length == 0 || offset + length > input.size()) {
            return false;
        }
        const auto nalu = input.subspan(offset, length);
        keyFrame = keyFrame || ((nalu.front() & 0x1fU) == 5);
        appendNalu(output, nalu);
        offset += length;
    }
    return true;
}

} // namespace

std::optional<VideoPacket> PacketParser::parseVideoSample(
    std::span<const std::uint8_t> sampleBuffer,
    std::string& error) {
    try {
        auto parsed = parseSample(sampleBuffer, true);
        if (parsed.videoFormat) {
            if (parsed.videoFormat->width != 0) {
                videoFormat_.width = parsed.videoFormat->width;
                videoFormat_.height = parsed.videoFormat->height;
            }
            if (!parsed.videoFormat->sps.empty()) {
                videoFormat_.sps = std::move(parsed.videoFormat->sps);
                videoFormat_.pps = std::move(parsed.videoFormat->pps);
            }
        }
        if (parsed.data.empty()) {
            return std::nullopt;
        }

        VideoPacket packet;
        packet.ptsNs = parsed.ptsNs;
        packet.format = videoFormat_;
        packet.data.reserve(parsed.data.size() + videoFormat_.sps.size() + videoFormat_.pps.size() + 12);
        if (parsed.videoFormat && !videoFormat_.sps.empty()) {
            appendNalu(packet.data, videoFormat_.sps);
            appendNalu(packet.data, videoFormat_.pps);
        }
        if (!convertAvccToAnnexB(parsed.data, packet.data, packet.keyFrame)) {
            error = "invalid AVCC NAL unit length";
            return std::nullopt;
        }
        return packet;
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

std::optional<AudioPacket> PacketParser::parseAudioSample(
    std::span<const std::uint8_t> sampleBuffer,
    std::string& error) {
    try {
        auto parsed = parseSample(sampleBuffer, false);
        if (parsed.audioFormat) {
            audioFormat_ = *parsed.audioFormat;
        }
        if (parsed.data.empty()) {
            return std::nullopt;
        }

        AudioPacket packet;
        packet.ptsNs = parsed.ptsNs;
        packet.data = std::move(parsed.data);
        packet.format = audioFormat_;
        return packet;
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

void PacketParser::reset() {
    videoFormat_ = {};
    audioFormat_ = {};
}

} // namespace padmirror::capture::usb
