#include "capture/usb/QuickTimeProtocol.h"

#include "capture/usb/BinaryIO.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace padmirror::capture::usb {
namespace {

using namespace binary;

constexpr std::uint32_t kPing = 0x70696e67;
constexpr std::uint32_t kSync = 0x73796e63;
constexpr std::uint32_t kReply = 0x72706c79;
constexpr std::uint32_t kAsync = 0x6173796e;

constexpr std::uint32_t kTime = 0x74696d65;
constexpr std::uint32_t kCwpa = 0x63777061;
constexpr std::uint32_t kAfmt = 0x61666d74;
constexpr std::uint32_t kCvrp = 0x63767270;
constexpr std::uint32_t kClok = 0x636c6f6b;
constexpr std::uint32_t kOg = 0x676f2120;
constexpr std::uint32_t kSkew = 0x736b6577;
constexpr std::uint32_t kStop = 0x73746f70;

constexpr std::uint32_t kFeed = 0x66656564;
constexpr std::uint32_t kRels = 0x72656c73;
constexpr std::uint32_t kHpd1 = 0x68706431;
constexpr std::uint32_t kHpa1 = 0x68706131;
constexpr std::uint32_t kNeed = 0x6e656564;
constexpr std::uint32_t kEat = 0x65617421;
constexpr std::uint32_t kHpd0 = 0x68706430;
constexpr std::uint32_t kHpa0 = 0x68706130;

constexpr std::uint32_t kKeyValue = 0x6b657976;
constexpr std::uint32_t kStringKey = 0x7374726b;
constexpr std::uint32_t kBooleanValue = 0x62756c76;
constexpr std::uint32_t kDictionary = 0x64696374;
constexpr std::uint32_t kDataValue = 0x64617476;
constexpr std::uint32_t kStringValue = 0x73747276;
constexpr std::uint32_t kNumberValue = 0x6e6d6276;
constexpr std::uint32_t kLinearPcm = 0x6c70636d;

std::vector<std::uint8_t> makeBlock(
    std::uint32_t magic,
    std::span<const std::uint8_t> payload = {}) {
    std::vector<std::uint8_t> result;
    result.reserve(payload.size() + 8);
    appendLe(result, static_cast<std::uint32_t>(payload.size() + 8));
    appendLe(result, magic);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::uint8_t> makeBoolValue(bool value) {
    const std::uint8_t encoded = value ? 1 : 0;
    return makeBlock(kBooleanValue, std::span(&encoded, 1));
}

std::vector<std::uint8_t> makeStringValue(const std::string& value) {
    return makeBlock(
        kStringValue,
        std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

std::vector<std::uint8_t> makeDataValue(std::span<const std::uint8_t> value) {
    return makeBlock(kDataValue, value);
}

std::vector<std::uint8_t> makeNumberU32(std::uint32_t value) {
    std::vector<std::uint8_t> payload;
    payload.push_back(3);
    appendLe(payload, value);
    return makeBlock(kNumberValue, payload);
}

std::vector<std::uint8_t> makeNumberDouble(double value) {
    std::vector<std::uint8_t> payload;
    payload.push_back(6);
    appendDoubleLe(payload, value);
    return makeBlock(kNumberValue, payload);
}

using DictionaryEntry = std::pair<std::string, std::vector<std::uint8_t>>;

std::vector<std::uint8_t> makeDictionary(std::vector<DictionaryEntry> entries) {
    std::vector<std::uint8_t> payload;
    for (auto& [key, value] : entries) {
        const auto keyBlock = makeBlock(
            kStringKey,
            std::span(reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
        std::vector<std::uint8_t> pairPayload;
        pairPayload.reserve(keyBlock.size() + value.size());
        pairPayload.insert(pairPayload.end(), keyBlock.begin(), keyBlock.end());
        pairPayload.insert(pairPayload.end(), value.begin(), value.end());
        const auto pair = makeBlock(kKeyValue, pairPayload);
        payload.insert(payload.end(), pair.begin(), pair.end());
    }
    return makeBlock(kDictionary, payload);
}

std::vector<std::uint8_t> makeAudioDescription() {
    std::vector<std::uint8_t> description;
    description.reserve(56);
    appendDoubleLe(description, 48000.0);
    appendLe(description, kLinearPcm);
    appendLe(description, std::uint32_t{12});
    appendLe(description, std::uint32_t{4});
    appendLe(description, std::uint32_t{1});
    appendLe(description, std::uint32_t{4});
    appendLe(description, std::uint32_t{2});
    appendLe(description, std::uint32_t{16});
    appendLe(description, std::uint32_t{0});
    appendDoubleLe(description, 48000.0);
    appendDoubleLe(description, 48000.0);
    return description;
}

std::vector<std::uint8_t> makeAsyncPacket(
    std::uint64_t clockRef,
    std::uint32_t subtype,
    std::span<const std::uint8_t> payload = {}) {
    std::vector<std::uint8_t> packet;
    packet.reserve(payload.size() + 20);
    appendLe(packet, static_cast<std::uint32_t>(payload.size() + 20));
    appendLe(packet, kAsync);
    appendLe(packet, clockRef);
    appendLe(packet, subtype);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::vector<std::uint8_t> makeHpd1Packet() {
    auto displaySize = makeDictionary({
        {"Width", makeNumberDouble(1920.0)},
        {"Height", makeNumberDouble(1200.0)},
    });
    auto details = makeDictionary({
        {"Valeria", makeBoolValue(true)},
        {"HEVCDecoderSupports444", makeBoolValue(true)},
        {"DisplaySize", std::move(displaySize)},
    });
    return makeAsyncPacket(1, kHpd1, details);
}

std::vector<std::uint8_t> makeHpa1Packet(std::uint64_t clockRef) {
    const auto audioDescription = makeAudioDescription();
    auto details = makeDictionary({
        {"BufferAheadInterval", makeNumberDouble(0.07300000000000001)},
        {"deviceUID", makeStringValue("Valeria")},
        {"ScreenLatency", makeNumberDouble(0.04)},
        {"formats", makeDataValue(audioDescription)},
        {"EDIDAC3Support", makeNumberU32(0)},
        {"deviceName", makeStringValue("Valeria")},
    });
    return makeAsyncPacket(clockRef, kHpa1, details);
}

std::vector<std::uint8_t> makePingReply() {
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{16});
    appendLe(packet, kPing);
    appendLe(packet, std::uint64_t{0x0000000100000000ULL});
    return packet;
}

std::vector<std::uint8_t> makeClockReply(std::uint64_t correlation, std::uint64_t clockRef) {
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{28});
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint32_t{0});
    appendLe(packet, clockRef);
    return packet;
}

std::vector<std::uint8_t> makeOgReply(std::uint64_t correlation) {
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{24});
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint64_t{0});
    return packet;
}

std::vector<std::uint8_t> makeTimeReply(
    std::uint64_t correlation,
    std::chrono::steady_clock::duration elapsed) {
    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{44});
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint32_t{0});
    appendLe(packet, static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsedNs)));
    appendLe(packet, std::uint32_t{1'000'000'000});
    appendLe(packet, std::uint32_t{1});
    appendLe(packet, std::uint64_t{0});
    return packet;
}

std::vector<std::uint8_t> makeAfmtReply(std::uint64_t correlation) {
    const auto dictionary = makeDictionary({{"Error", makeNumberU32(0)}});
    std::vector<std::uint8_t> packet;
    packet.reserve(dictionary.size() + 20);
    appendLe(packet, static_cast<std::uint32_t>(dictionary.size() + 20));
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint32_t{0});
    packet.insert(packet.end(), dictionary.begin(), dictionary.end());
    return packet;
}

std::vector<std::uint8_t> makeSkewReply(std::uint64_t correlation, double skew) {
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{28});
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint32_t{0});
    appendDoubleLe(packet, skew);
    return packet;
}

std::vector<std::uint8_t> makeStopReply(std::uint64_t correlation) {
    std::vector<std::uint8_t> packet;
    appendLe(packet, std::uint32_t{24});
    appendLe(packet, kReply);
    appendLe(packet, correlation);
    appendLe(packet, std::uint64_t{0});
    return packet;
}

std::vector<std::uint8_t> makeNeedPacket(std::uint64_t clockRef) {
    return makeAsyncPacket(clockRef, kNeed);
}

std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count()));
}

} // namespace

void QuickTimeProtocol::setWriter(Writer writer) {
    writer_ = std::move(writer);
}

void QuickTimeProtocol::setVideoHandler(VideoHandler handler) {
    videoHandler_ = std::move(handler);
}

void QuickTimeProtocol::setAudioHandler(AudioHandler handler) {
    audioHandler_ = std::move(handler);
}

void QuickTimeProtocol::setErrorHandler(MessageHandler handler) {
    errorHandler_ = std::move(handler);
}

void QuickTimeProtocol::setReadyHandler(ReadyHandler handler) {
    readyHandler_ = std::move(handler);
}

bool QuickTimeProtocol::processFrame(std::span<const std::uint8_t> frame) {
    try {
        if (frame.size() < 4) {
            throw std::runtime_error("truncated USB protocol frame");
        }
        switch (readLe<std::uint32_t>(frame)) {
        case kPing:
            if (!send(makePingReply())) {
                return false;
            }
            if (readyHandler_) {
                readyHandler_();
            }
            return true;
        case kSync:
            return processSync(frame);
        case kAsync:
            return processAsync(frame);
        default:
            reportError("unsupported QuickTime USB packet type");
            return true;
        }
    } catch (const std::exception& exception) {
        reportError(exception.what());
        return false;
    }
}

bool QuickTimeProtocol::processSync(std::span<const std::uint8_t> frame) {
    if (frame.size() < 24) {
        throw std::runtime_error("truncated SYNC packet");
    }
    const auto clockRef = readLe<std::uint64_t>(frame, 4);
    const auto subtype = readLe<std::uint32_t>(frame, 12);
    const auto correlation = readLe<std::uint64_t>(frame, 16);

    switch (subtype) {
    case kOg:
        return send(makeOgReply(correlation));
    case kCwpa: {
        if (frame.size() < 32) {
            throw std::runtime_error("truncated CWPA packet");
        }
        deviceAudioClockRef_ = readLe<std::uint64_t>(frame, 24);
        audioClockStart_ = std::chrono::steady_clock::now();
        audioClockRunning_ = true;
        const auto localClockRef = deviceAudioClockRef_ + 1000;
        const auto display = makeHpd1Packet();
        return send(display) &&
            send(makeClockReply(correlation, localClockRef)) &&
            send(display) &&
            send(makeHpa1Packet(deviceAudioClockRef_));
    }
    case kCvrp: {
        if (frame.size() < 32) {
            throw std::runtime_error("truncated CVRP packet");
        }
        const auto deviceClock = readLe<std::uint64_t>(frame, 24);
        needPacket_ = makeNeedPacket(deviceClock);
        return send(needPacket_) && send(makeClockReply(correlation, deviceClock + 0x1000af));
    }
    case kClok:
        hostClockStart_ = std::chrono::steady_clock::now();
        hostClockRunning_ = true;
        return send(makeClockReply(correlation, clockRef + 0x10000));
    case kTime:
        if (!hostClockRunning_) {
            hostClockStart_ = std::chrono::steady_clock::now();
            hostClockRunning_ = true;
        }
        return send(makeTimeReply(correlation, std::chrono::steady_clock::now() - hostClockStart_));
    case kAfmt:
        return send(makeAfmtReply(correlation));
    case kSkew:
        return send(makeSkewReply(correlation, currentSkew()));
    case kStop:
        return send(makeStopReply(correlation));
    default:
        reportError("unknown SYNC subtype; packet ignored for forward compatibility");
        return true;
    }
}

bool QuickTimeProtocol::processAsync(std::span<const std::uint8_t> frame) {
    if (frame.size() < 16) {
        throw std::runtime_error("truncated ASYN packet");
    }
    const auto subtype = readLe<std::uint32_t>(frame, 12);
    if (subtype == kFeed) {
        std::string error;
        auto packet = parser_.parseVideoSample(frame.subspan(16), error);
        if (!error.empty()) {
            reportError("video packet: " + error);
        }
        if (packet && videoHandler_) {
            videoHandler_(std::move(*packet));
        }
        return needPacket_.empty() || send(needPacket_);
    }
    if (subtype == kEat) {
        std::string error;
        auto packet = parser_.parseAudioSample(frame.subspan(16), error);
        if (!error.empty()) {
            reportError("audio packet: " + error);
        }
        if (packet) {
            updateAudioClock(*packet);
            if (audioHandler_) {
                audioHandler_(std::move(*packet));
            }
        }
        return true;
    }
    if (subtype == kRels) {
        {
            std::lock_guard lock(releaseMutex_);
            ++releaseCount_;
        }
        releaseCondition_.notify_all();
        return true;
    }

    // SPRP/TJMP/SRAT/TBAS and future advisory messages require no response.
    return true;
}

void QuickTimeProtocol::beginClose() {
    if (!writer_) {
        return;
    }
    send(makeAsyncPacket(deviceAudioClockRef_, kHpa0));
    send(makeAsyncPacket(1, kHpd0));
}

bool QuickTimeProtocol::waitForRelease(std::chrono::milliseconds timeout) {
    std::unique_lock lock(releaseMutex_);
    return releaseCondition_.wait_for(lock, timeout, [this] {
        return releaseCount_ >= 2;
    });
}

void QuickTimeProtocol::finishClose() {
    if (writer_) {
        send(makeAsyncPacket(1, kHpd0));
    }
}

void QuickTimeProtocol::reset() {
    parser_.reset();
    needPacket_.clear();
    deviceAudioClockRef_ = 0;
    hostClockRunning_ = false;
    audioClockRunning_ = false;
    hasAudioTiming_ = false;
    audioSampleRate_ = 48000;
    firstAudioDeviceNs_ = 0;
    lastAudioDeviceNs_ = 0;
    firstAudioHostNs_ = 0;
    lastAudioHostNs_ = 0;
    std::lock_guard lock(releaseMutex_);
    releaseCount_ = 0;
}

bool QuickTimeProtocol::send(const std::vector<std::uint8_t>& packet) {
    if (!writer_) {
        reportError("USB writer is not available");
        return false;
    }
    if (!writer_(packet)) {
        reportError("USB write failed");
        return false;
    }
    return true;
}

void QuickTimeProtocol::reportError(const std::string& message) const {
    if (errorHandler_) {
        errorHandler_(message);
    }
}

void QuickTimeProtocol::updateAudioClock(const AudioPacket& packet) {
    if (!audioClockRunning_ || packet.ptsNs == 0) {
        return;
    }
    if (packet.format.sampleRate != 0) {
        audioSampleRate_ = packet.format.sampleRate;
    }
    const auto hostNs = elapsedNanoseconds(audioClockStart_);
    if (!hasAudioTiming_) {
        firstAudioDeviceNs_ = packet.ptsNs;
        firstAudioHostNs_ = hostNs;
        hasAudioTiming_ = true;
    }
    lastAudioDeviceNs_ = packet.ptsNs;
    lastAudioHostNs_ = hostNs;
}

double QuickTimeProtocol::currentSkew() const {
    if (!hasAudioTiming_ || lastAudioDeviceNs_ <= firstAudioDeviceNs_ ||
        lastAudioHostNs_ <= firstAudioHostNs_) {
        return static_cast<double>(audioSampleRate_);
    }
    const auto deviceDelta = static_cast<double>(lastAudioDeviceNs_ - firstAudioDeviceNs_);
    const auto hostDelta = static_cast<double>(lastAudioHostNs_ - firstAudioHostNs_);
    const auto rateRatio = hostDelta / deviceDelta;
    return std::isfinite(rateRatio) && rateRatio > 0.5 && rateRatio < 1.5
        ? static_cast<double>(audioSampleRate_) * rateRatio
        : static_cast<double>(audioSampleRate_);
}

} // namespace padmirror::capture::usb
