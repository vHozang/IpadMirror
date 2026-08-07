#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace padmirror::capture::usb {

class UsbTransport {
public:
    struct Device {
        std::string serial;
        std::string productName;
        std::uint16_t vendorId = 0;
        std::uint16_t productId = 0;
        int usbMuxConfiguration = -1;
        int quickTimeConfiguration = -1;
    };

    enum class RunResult {
        Stopped,
        Disconnected,
        Failed,
    };

    using FrameHandler = std::function<bool(std::span<const std::uint8_t>)>;
    using ErrorHandler = std::function<void(const std::string&)>;

    UsbTransport() = default;
    ~UsbTransport();

    UsbTransport(const UsbTransport&) = delete;
    UsbTransport& operator=(const UsbTransport&) = delete;

    static std::vector<Device> enumerate(std::string& error);

    RunResult run(
        const std::string& preferredSerial,
        FrameHandler frameHandler,
        ErrorHandler errorHandler,
        std::function<bool()> shouldStop);

    bool write(std::span<const std::uint8_t> packet);
    void requestStop();
    [[nodiscard]] bool connected() const;

private:
    void closeHandle();

    mutable std::mutex ioMutex_;
    libusb_context* context_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    std::uint8_t outputEndpoint_ = 0;
    int claimedInterface_ = -1;
    int usbMuxConfiguration_ = -1;
    std::atomic_bool requestedStop_{false};
    std::atomic_bool connected_{false};
};

} // namespace padmirror::capture::usb
