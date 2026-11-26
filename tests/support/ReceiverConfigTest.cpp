#include "support/ReceiverConfig.h"

#include <cassert>
#include <string>

int main() {
    using padmirror::support::QualityProfile;
    using padmirror::support::ReceiverConfig;
    using padmirror::support::normalizeReceiverConfig;
    using padmirror::support::qualityHeight;
    using padmirror::support::qualityWidth;
    using padmirror::support::receiverRestartRequired;
    using padmirror::support::sanitizeReceiverName;

    assert(sanitizeReceiverName("") == "PadMirror");
    assert(sanitizeReceiverName("  Living\t Room\n") == "Living Room");
    assert(sanitizeReceiverName(std::string(80, 'x')).size() == 63);

    ReceiverConfig invalid;
    invalid.maxFps = 42;
    invalid.qualityProfile = static_cast<QualityProfile>(3);
    invalid.volumePercent = 140;
    auto normalized = normalizeReceiverConfig(invalid);
    assert(normalized.maxFps == 60);
    assert(normalized.qualityProfile == QualityProfile::FHD_1080);
    assert(normalized.volumePercent == 100);

    assert(qualityWidth(QualityProfile::HD_720) == 1280);
    assert(qualityHeight(QualityProfile::HD_720) == 720);
    assert(qualityWidth(QualityProfile::FHD_1080) == 1920);
    assert(qualityHeight(QualityProfile::FHD_1080) == 1080);
    assert(qualityWidth(QualityProfile::QHD_1440) == 2560);
    assert(qualityHeight(QualityProfile::QHD_1440) == 1440);

    ReceiverConfig changed = normalized;
    changed.volumePercent = 25;
    assert(!receiverRestartRequired(normalized, changed));
    changed.maxFps = 30;
    assert(normalizeReceiverConfig(changed).maxFps == 60);
    assert(!receiverRestartRequired(normalized, changed));

    changed = normalized;
    changed.qualityProfile = QualityProfile::HD_720;
    assert(receiverRestartRequired(normalized, changed));
    changed.qualityProfile = normalized.qualityProfile;
    assert(!receiverRestartRequired(normalized, changed));

    ReceiverConfig netConfig;
    netConfig.bindAddress = "  192.168.0.224  ";
    auto normalizedNet = normalizeReceiverConfig(netConfig);
    assert(normalizedNet.bindAddress == "192.168.0.224");

    ReceiverConfig netChanged = normalizedNet;
    netChanged.bindAddress = "192.168.1.10";
    assert(receiverRestartRequired(normalizedNet, netChanged));

    ReceiverConfig netInvalid = netConfig;
    netInvalid.bindAddress = "invalid.ip";
    assert(normalizeReceiverConfig(netInvalid).bindAddress.empty());

    ReceiverConfig netAuto = netConfig;
    netAuto.bindAddress = "auto";
    assert(normalizeReceiverConfig(netAuto).bindAddress.empty());

    return 0;
}
