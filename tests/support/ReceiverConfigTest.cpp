#include "support/ReceiverConfig.h"

#include <cassert>
#include <string>

int main() {
    using padmirror::support::ReceiverConfig;
    using padmirror::support::normalizeReceiverConfig;
    using padmirror::support::receiverRestartRequired;
    using padmirror::support::sanitizeReceiverName;

    assert(sanitizeReceiverName("") == "PadMirror");
    assert(sanitizeReceiverName("  Living\t Room\n") == "Living Room");
    assert(sanitizeReceiverName(std::string(80, 'x')).size() == 63);

    ReceiverConfig invalid;
    invalid.maxFps = 42;
    invalid.volumePercent = 140;
    auto normalized = normalizeReceiverConfig(invalid);
    assert(normalized.maxFps == 60);
    assert(normalized.volumePercent == 100);

    ReceiverConfig changed = normalized;
    changed.volumePercent = 25;
    assert(!receiverRestartRequired(normalized, changed));
    changed.maxFps = 30;
    assert(normalizeReceiverConfig(changed).maxFps == 60);
    assert(!receiverRestartRequired(normalized, changed));
    return 0;
}
