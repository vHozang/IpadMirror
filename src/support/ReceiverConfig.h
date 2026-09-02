#pragma once

#include <string>

namespace padmirror::support {

struct ReceiverConfig {
    std::string receiverName = "PadMirror";
    int maxFps = 60;
    int volumePercent = 100;
    bool muted = false;
    bool keepAwakeWhileStreaming = true;
    bool startReceiverOnLaunch = true;
    bool fullscreenOnConnect = false;

    friend bool operator==(const ReceiverConfig&, const ReceiverConfig&) = default;
};

[[nodiscard]] std::string sanitizeReceiverName(const std::string& value);
[[nodiscard]] ReceiverConfig normalizeReceiverConfig(ReceiverConfig config);
[[nodiscard]] bool receiverRestartRequired(const ReceiverConfig& before,
                                           const ReceiverConfig& after);

} // namespace padmirror::support
