#pragma once

#include "core/IMirrorBackend.h"

#include <mutex>

namespace padmirror::capture::usb {

class UsbMirrorBackend final : public core::IMirrorBackend {
public:
    bool initialize() override;
    void shutdown() override;
    bool start() override;
    void stop() override;
    [[nodiscard]] core::MirrorState state() const override;
    [[nodiscard]] core::MirrorHealth health() const override;

private:
    mutable std::mutex mutex_;
    core::MirrorState state_ = core::MirrorState::Stopped;
    core::MirrorHealth health_;
};

} // namespace padmirror::capture::usb
