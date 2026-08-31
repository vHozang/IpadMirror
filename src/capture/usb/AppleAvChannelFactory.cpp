#include "capture/usb/AppleAvChannelFactory.h"

namespace padmirror::capture::usb {

AppleAvChannelFactory::Result AppleAvChannelFactory::create() {
    const auto runtime = AppleRuntimeProbe::probe();
    if (!runtime.appleRuntimeAvailable) {
        return {
            .status = Status::UsbCaptureUnsupported,
            .detail = "USB_CAPTURE_UNSUPPORTED: " + runtime.detail,
        };
    }
    if (!runtime.userModeAvChannelAvailable) {
        return {
            .status = Status::UsbCaptureUnsupported,
            .detail = "USB_CAPTURE_UNSUPPORTED: " + runtime.detail +
                ". PadMirror will not replace, rebind, or filter Apple USB drivers.",
        };
    }
    return {.status = Status::Available, .detail = "Apple-supported AV channel is available"};
}

} // namespace padmirror::capture::usb
