#pragma once

#include <string>

namespace padmirror::capture::usb {

class AppleRuntimeProbe final {
public:
    struct Result {
        bool appleRuntimeAvailable = false;
        bool userModeAvChannelAvailable = false;
        std::string detail;
    };

    [[nodiscard]] static Result probe();
};

} // namespace padmirror::capture::usb
