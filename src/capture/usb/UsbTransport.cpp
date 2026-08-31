#include "capture/usb/UsbTransport.h"

#include "capture/usb/AppleAvChannelFactory.h"

namespace padmirror::capture::usb {

std::vector<UsbTransport::Device> UsbTransport::enumerate(std::string& error) {
    error.clear();
    return {};
}

UsbTransport::RunResult UsbTransport::run(
    const std::string& preferredSerial,
    FrameHandler frameHandler,
    ErrorHandler errorHandler,
    std::function<bool()> shouldStop) {
    (void)preferredSerial;
    (void)frameHandler;
    requestedStop_.store(false);
    connected_.store(false);
    if (shouldStop && shouldStop()) return RunResult::Stopped;

    const auto channel = AppleAvChannelFactory::create();
    if (channel.status != AppleAvChannelFactory::Status::Available) {
        if (errorHandler) errorHandler(channel.detail);
        return RunResult::Failed;
    }

    // A channel implementation may be added only after Apple exposes a stable,
    // supported user-mode AV API. There is deliberately no WinUSB/libusb path.
    if (errorHandler) {
        errorHandler("USB_CAPTURE_UNSUPPORTED: Apple AV channel implementation is unavailable");
    }
    return RunResult::Failed;
}

bool UsbTransport::write(std::span<const std::uint8_t> packet) {
    (void)packet;
    return false;
}

void UsbTransport::requestStop() {
    requestedStop_.store(true);
    connected_.store(false);
}

bool UsbTransport::connected() const {
    return connected_.load();
}

} // namespace padmirror::capture::usb
