#pragma once

#include "capture/usb/AppleRuntimeProbe.h"

#include <string>

namespace padmirror::capture::usb {

class AppleAvChannelFactory final {
public:
    enum class Status {
        Available,
        UsbCaptureUnsupported,
    };

    struct Result {
        Status status = Status::UsbCaptureUnsupported;
        std::string detail;
    };

    [[nodiscard]] static Result create();
};

} // namespace padmirror::capture::usb
