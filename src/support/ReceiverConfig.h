#pragma once

#include <string>

namespace padmirror::support {

enum class QualityProfile { HD_720 = 0, FHD_1080 = 1, QHD_1440 = 2 };

[[nodiscard]] inline int qualityWidth(QualityProfile profile) {
    switch (profile) {
    case QualityProfile::HD_720:   return 1280;
    case QualityProfile::FHD_1080: return 1920;
    case QualityProfile::QHD_1440: return 2560;
    }
    return 1920;
}

[[nodiscard]] inline int qualityHeight(QualityProfile profile) {
    switch (profile) {
    case QualityProfile::HD_720:   return 720;
    case QualityProfile::FHD_1080: return 1080;
    case QualityProfile::QHD_1440: return 1440;
    }
    return 1080;
}

struct ReceiverConfig {
    std::string receiverName = "PadMirror";
    QualityProfile qualityProfile = QualityProfile::FHD_1080;
    std::string bindAddress = "";
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
