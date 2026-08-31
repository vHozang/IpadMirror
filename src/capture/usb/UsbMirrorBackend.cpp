#include "capture/usb/UsbMirrorBackend.h"

#include "capture/usb/AppleAvChannelFactory.h"

namespace padmirror::capture::usb {

bool UsbMirrorBackend::initialize() {
    const auto channel = AppleAvChannelFactory::create();
    std::lock_guard lock(mutex_);
    health_ = {
        .available = channel.status == AppleAvChannelFactory::Status::Available,
        .trusted = false,
        .detail = channel.detail,
    };
    state_ = health_.available ? core::MirrorState::Waiting : core::MirrorState::Error;
    return health_.available;
}

void UsbMirrorBackend::shutdown() {
    stop();
}

bool UsbMirrorBackend::start() {
    if (!initialize()) return false;

    std::lock_guard lock(mutex_);
    // A concrete session is permitted only when Apple exposes a documented
    // user-mode AV channel. The legacy driver-rebinding transport is forbidden.
    state_ = core::MirrorState::Error;
    health_.available = false;
    health_.detail = "USB_CAPTURE_UNSUPPORTED: Apple user-mode AV channel implementation is unavailable";
    return false;
}

void UsbMirrorBackend::stop() {
    std::lock_guard lock(mutex_);
    if (state_ != core::MirrorState::Error) state_ = core::MirrorState::Stopped;
}

core::MirrorState UsbMirrorBackend::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

core::MirrorHealth UsbMirrorBackend::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

} // namespace padmirror::capture::usb
