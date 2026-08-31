#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace padmirror::capture::usb {

// Transport is intentionally a no-op unless an Apple-supported user-mode AV
// channel is supplied by Apple Devices. It must never rebind or filter drivers.
class UsbTransport {
public:
    struct Device {
        std::string serial;
        std::string productName;
        std::uint16_t vendorId = 0;
        std::uint16_t productId = 0;
    };

    enum class RunResult {
        Stopped,
        Disconnected,
        Failed,
    };

    using FrameHandler = std::function<bool(std::span<const std::uint8_t>)>;
    using ErrorHandler = std::function<void(const std::string&)>;

    UsbTransport() = default;
    ~UsbTransport() = default;

    UsbTransport(const UsbTransport&) = delete;
    UsbTransport& operator=(const UsbTransport&) = delete;

    [[nodiscard]] static std::vector<Device> enumerate(std::string& error);
    RunResult run(
        const std::string& preferredSerial,
        FrameHandler frameHandler,
        ErrorHandler errorHandler,
        std::function<bool()> shouldStop);

    bool write(std::span<const std::uint8_t> packet);
    void requestStop();
    [[nodiscard]] bool connected() const;

private:
    std::atomic_bool requestedStop_{false};
    std::atomic_bool connected_{false};
};

} // namespace padmirror::capture::usb
