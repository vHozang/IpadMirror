#include "support/ReceiverConfig.h"

#include <algorithm>
#include <cctype>

namespace padmirror::support {
namespace {

bool isAsciiControl(unsigned char value) {
    return value < 0x20U || value == 0x7fU;
}

std::size_t utf8PrefixLength(const std::string& value, std::size_t maximumBytes) {
    auto length = std::min(value.size(), maximumBytes);
    while (length > 0 && length < value.size() &&
           (static_cast<unsigned char>(value[length]) & 0xc0U) == 0x80U) {
        --length;
    }
    return length;
}

} // namespace

std::string sanitizeReceiverName(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    bool pendingSpace = false;

    for (const unsigned char byte : value) {
        if (isAsciiControl(byte)) {
            if (!result.empty()) {
                pendingSpace = true;
            }
            continue;
        }
        if (byte < 0x80U && std::isspace(byte)) {
            if (!result.empty()) {
                pendingSpace = true;
            }
            continue;
        }
        if (pendingSpace && !result.empty()) {
            result.push_back(' ');
        }
        pendingSpace = false;
        result.push_back(static_cast<char>(byte));
    }

    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    if (result.empty()) {
        return "PadMirror";
    }
    result.resize(utf8PrefixLength(result, 63));
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result.empty() ? "PadMirror" : result;
}

ReceiverConfig normalizeReceiverConfig(ReceiverConfig config) {
    config.receiverName = sanitizeReceiverName(config.receiverName);
    // V1 has one supported capture cadence. Keeping this fixed at 60 also
    // upgrades settings written by earlier builds that allowed a 30 FPS cap.
    config.maxFps = 60;
    config.volumePercent = std::clamp(config.volumePercent, 0, 100);
    return config;
}

bool receiverRestartRequired(const ReceiverConfig& before,
                             const ReceiverConfig& after) {
    const auto normalizedBefore = normalizeReceiverConfig(before);
    const auto normalizedAfter = normalizeReceiverConfig(after);
    return normalizedBefore.receiverName != normalizedAfter.receiverName ||
           normalizedBefore.maxFps != normalizedAfter.maxFps;
}

} // namespace padmirror::support
