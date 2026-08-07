#include "capture/usb/UsbTransport.h"

#include "capture/usb/BinaryIO.h"

#include <libusb.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>
#include <type_traits>

namespace padmirror::capture::usb {
namespace {

constexpr std::uint16_t kAppleVendorId = 0x05ac;
constexpr std::uint8_t kVendorInterfaceClass = 0xff;
constexpr std::uint8_t kUsbMuxSubclass = 0xfe;
constexpr std::uint8_t kQuickTimeSubclass = 0x2a;
constexpr std::size_t kMaximumFrameBytes = 32U * 1024U * 1024U;

struct InterfaceEndpoints {
    int interfaceNumber = -1;
    int alternateSetting = 0;
    std::uint8_t inputEndpoint = 0;
    std::uint8_t outputEndpoint = 0;
};

std::string usbError(int code) {
    const auto* name = libusb_error_name(code);
    return name ? name : "unknown libusb error";
}

int initializeUsbContext(libusb_context** context) {
#ifdef _WIN32
    libusb_init_option option{};
    option.option = LIBUSB_OPTION_USE_USBDK;
    option.value.ival = 1;
    const auto usbDkResult = libusb_init_context(context, &option, 1);
    if (usbDkResult == LIBUSB_SUCCESS) return usbDkResult;
    *context = nullptr;
#endif
    return libusb_init(context);
}

std::string readUsbString(libusb_device_handle* handle, std::uint8_t index) {
    if (!handle || index == 0) {
        return {};
    }
    unsigned char buffer[256]{};
    const auto size = libusb_get_string_descriptor_ascii(handle, index, buffer, sizeof(buffer));
    return size > 0 ? std::string(reinterpret_cast<char*>(buffer), static_cast<std::size_t>(size)) : std::string{};
}

bool configurationHasSubclass(
    const libusb_config_descriptor* configuration,
    std::uint8_t subclass) {
    for (int interfaceIndex = 0; interfaceIndex < configuration->bNumInterfaces; ++interfaceIndex) {
        const auto& interface = configuration->interface[interfaceIndex];
        for (int alternateIndex = 0; alternateIndex < interface.num_altsetting; ++alternateIndex) {
            const auto& alternate = interface.altsetting[alternateIndex];
            if (alternate.bInterfaceClass == kVendorInterfaceClass &&
                alternate.bInterfaceSubClass == subclass) {
                return true;
            }
        }
    }
    return false;
}

UsbTransport::Device describeDevice(libusb_device* device, libusb_device_handle* optionalHandle = nullptr) {
    UsbTransport::Device result;
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
        return result;
    }
    result.vendorId = descriptor.idVendor;
    result.productId = descriptor.idProduct;

    for (std::uint8_t index = 0; index < descriptor.bNumConfigurations; ++index) {
        libusb_config_descriptor* configuration = nullptr;
        if (libusb_get_config_descriptor(device, index, &configuration) != LIBUSB_SUCCESS) {
            continue;
        }
        const bool hasMux = configurationHasSubclass(configuration, kUsbMuxSubclass);
        const bool hasQuickTime = configurationHasSubclass(configuration, kQuickTimeSubclass);
        if (hasMux && !hasQuickTime) {
            result.usbMuxConfiguration = configuration->bConfigurationValue;
        }
        if (hasQuickTime) {
            result.quickTimeConfiguration = configuration->bConfigurationValue;
        }
        libusb_free_config_descriptor(configuration);
    }

    libusb_device_handle* handle = optionalHandle;
    bool closeWhenDone = false;
    if (!handle && libusb_open(device, &handle) == LIBUSB_SUCCESS) {
        closeWhenDone = true;
    }
    if (handle) {
        result.serial = readUsbString(handle, descriptor.iSerialNumber);
        result.productName = readUsbString(handle, descriptor.iProduct);
    }
    if (closeWhenDone) {
        libusb_close(handle);
    }
    if (result.productName.empty()) {
        result.productName = "iPad / iOS device";
    }
    return result;
}

bool isCaptureCandidate(libusb_device* device) {
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS ||
        descriptor.idVendor != kAppleVendorId) {
        return false;
    }
    const auto info = describeDevice(device);
    return info.usbMuxConfiguration >= 0 || info.quickTimeConfiguration >= 0;
}

struct OpenedDevice {
    UsbTransport::Device info;
    libusb_device_handle* handle = nullptr;
};

std::optional<OpenedDevice> openDevice(
    libusb_context* context,
    const std::string& preferredSerial,
    std::string& error) {
    libusb_device** devices = nullptr;
    const auto count = libusb_get_device_list(context, &devices);
    if (count < 0) {
        error = "Cannot enumerate USB devices: " + usbError(static_cast<int>(count));
        return std::nullopt;
    }

    std::optional<OpenedDevice> selected;
    for (std::remove_cv_t<decltype(count)> index = 0; index < count; ++index) {
        auto* device = devices[index];
        if (!isCaptureCandidate(device)) {
            continue;
        }
        libusb_device_handle* handle = nullptr;
        const auto openResult = libusb_open(device, &handle);
        if (openResult != LIBUSB_SUCCESS) {
#ifdef _WIN32
            if (openResult == LIBUSB_ERROR_ACCESS) {
                error = "Access to the iPad USB interface was denied by Windows.";
            } else if (openResult == LIBUSB_ERROR_NOT_FOUND) {
                error = "The UsbDk capture driver could not open the iPad. Reconnect the cable or restart Windows once.";
            } else {
                error = "Cannot open iPad USB interface: " + usbError(openResult);
            }
#else
            error = "Cannot open iPad USB interface: " + usbError(openResult);
#endif
            continue;
        }
        auto info = describeDevice(device, handle);
        const bool matches = preferredSerial.empty() || info.serial == preferredSerial;
        if (matches) {
            selected = OpenedDevice{std::move(info), handle};
            break;
        }
        libusb_close(handle);
    }
    libusb_free_device_list(devices, 1);
    if (!selected && error.empty()) {
        error = preferredSerial.empty()
            ? "No iPad was found on USB"
            : "The selected iPad is no longer connected";
    }
    return selected;
}

std::optional<InterfaceEndpoints> findQuickTimeInterface(
    libusb_device* device,
    int configurationValue) {
    libusb_config_descriptor* configuration = nullptr;
    if (libusb_get_config_descriptor_by_value(
            device,
            static_cast<std::uint8_t>(configurationValue),
            &configuration) != LIBUSB_SUCCESS) {
        return std::nullopt;
    }

    std::optional<InterfaceEndpoints> result;
    for (int interfaceIndex = 0; interfaceIndex < configuration->bNumInterfaces && !result; ++interfaceIndex) {
        const auto& interface = configuration->interface[interfaceIndex];
        for (int alternateIndex = 0; alternateIndex < interface.num_altsetting; ++alternateIndex) {
            const auto& alternate = interface.altsetting[alternateIndex];
            if (alternate.bInterfaceClass != kVendorInterfaceClass ||
                alternate.bInterfaceSubClass != kQuickTimeSubclass) {
                continue;
            }
            InterfaceEndpoints endpoints;
            endpoints.interfaceNumber = alternate.bInterfaceNumber;
            endpoints.alternateSetting = alternate.bAlternateSetting;
            for (std::uint8_t endpointIndex = 0; endpointIndex < alternate.bNumEndpoints; ++endpointIndex) {
                const auto& endpoint = alternate.endpoint[endpointIndex];
                if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }
                if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                    endpoints.inputEndpoint = endpoint.bEndpointAddress;
                } else {
                    endpoints.outputEndpoint = endpoint.bEndpointAddress;
                }
            }
            if (endpoints.inputEndpoint != 0 && endpoints.outputEndpoint != 0) {
                result = endpoints;
                break;
            }
        }
    }
    libusb_free_config_descriptor(configuration);
    return result;
}

bool waitForReconnect(const std::function<bool()>& shouldStop, std::chrono::milliseconds duration) {
    constexpr auto step = std::chrono::milliseconds(50);
    for (auto waited = std::chrono::milliseconds(0); waited < duration; waited += step) {
        if (shouldStop()) {
            return false;
        }
        std::this_thread::sleep_for(step);
    }
    return true;
}

} // namespace

UsbTransport::~UsbTransport() {
    requestStop();
    closeHandle();
}

std::vector<UsbTransport::Device> UsbTransport::enumerate(std::string& error) {
    libusb_context* context = nullptr;
    const auto initResult = initializeUsbContext(&context);
    if (initResult != LIBUSB_SUCCESS) {
        error = "Cannot initialize libusb: " + usbError(initResult);
        return {};
    }

    libusb_device** devices = nullptr;
    const auto count = libusb_get_device_list(context, &devices);
    std::vector<Device> result;
    if (count < 0) {
        error = "Cannot enumerate USB devices: " + usbError(static_cast<int>(count));
    } else {
        for (std::remove_cv_t<decltype(count)> index = 0; index < count; ++index) {
            if (isCaptureCandidate(devices[index])) {
                result.push_back(describeDevice(devices[index]));
            }
        }
    }
    if (devices) {
        libusb_free_device_list(devices, 1);
    }
    libusb_exit(context);
    return result;
}

UsbTransport::RunResult UsbTransport::run(
    const std::string& preferredSerial,
    FrameHandler frameHandler,
    ErrorHandler errorHandler,
    std::function<bool()> shouldStop) {
    requestedStop_.store(false);
    closeHandle();
    const auto initResult = initializeUsbContext(&context_);
    if (initResult != LIBUSB_SUCCESS) {
        errorHandler("Cannot initialize libusb: " + usbError(initResult));
        context_ = nullptr;
        return RunResult::Failed;
    }

    std::string openError;
    auto opened = openDevice(context_, preferredSerial, openError);
    if (!opened) {
        errorHandler(openError);
        closeHandle();
        return RunResult::Disconnected;
    }

    if (opened->info.quickTimeConfiguration < 0) {
        const auto activateResult = libusb_control_transfer(
            opened->handle,
            0x40,
            0x52,
            0,
            2,
            nullptr,
            0,
            1000);
        if (activateResult < 0 && activateResult != LIBUSB_ERROR_NO_DEVICE) {
            errorHandler("Cannot enable the iPad QuickTime USB configuration: " + usbError(activateResult));
        }
        libusb_close(opened->handle);
        opened.reset();

        for (int attempt = 0; attempt < 20 && !shouldStop(); ++attempt) {
            if (!waitForReconnect(shouldStop, std::chrono::milliseconds(400))) {
                break;
            }
            openError.clear();
            opened = openDevice(context_, preferredSerial, openError);
            if (opened && opened->info.quickTimeConfiguration >= 0) {
                break;
            }
            if (opened) {
                libusb_close(opened->handle);
                opened.reset();
            }
        }
        if (!opened || opened->info.quickTimeConfiguration < 0) {
            errorHandler("The iPad did not reconnect with the QuickTime capture interface");
            closeHandle();
            return shouldStop() ? RunResult::Stopped : RunResult::Failed;
        }
    }

    auto* handle = opened->handle;
    const auto quickTimeConfiguration = opened->info.quickTimeConfiguration;
    usbMuxConfiguration_ = opened->info.usbMuxConfiguration;
    const auto setConfigurationResult = libusb_set_configuration(handle, quickTimeConfiguration);
    if (setConfigurationResult != LIBUSB_SUCCESS && setConfigurationResult != LIBUSB_ERROR_BUSY) {
        errorHandler("Cannot select the QuickTime USB configuration: " + usbError(setConfigurationResult));
        libusb_close(handle);
        closeHandle();
        return RunResult::Failed;
    }

    const auto endpoints = findQuickTimeInterface(libusb_get_device(handle), quickTimeConfiguration);
    if (!endpoints) {
        errorHandler("QuickTime bulk endpoints were not found on the iPad");
        libusb_close(handle);
        closeHandle();
        return RunResult::Failed;
    }

    libusb_set_auto_detach_kernel_driver(handle, 1);
    const auto claimResult = libusb_claim_interface(handle, endpoints->interfaceNumber);
    if (claimResult != LIBUSB_SUCCESS) {
        errorHandler("Cannot claim the iPad QuickTime interface: " + usbError(claimResult));
        libusb_close(handle);
        closeHandle();
        return RunResult::Failed;
    }
    if (endpoints->alternateSetting != 0) {
        libusb_set_interface_alt_setting(handle, endpoints->interfaceNumber, endpoints->alternateSetting);
    }
    libusb_clear_halt(handle, endpoints->inputEndpoint);
    libusb_clear_halt(handle, endpoints->outputEndpoint);

    {
        std::lock_guard lock(ioMutex_);
        handle_ = handle;
        outputEndpoint_ = endpoints->outputEndpoint;
        claimedInterface_ = endpoints->interfaceNumber;
    }
    connected_.store(true);

    std::vector<std::uint8_t> transferBuffer(1024U * 1024U);
    std::vector<std::uint8_t> streamBuffer;
    streamBuffer.reserve(2U * 1024U * 1024U);
    RunResult result = RunResult::Stopped;

    while (!requestedStop_.load() && !shouldStop()) {
        int transferred = 0;
        const auto readResult = libusb_bulk_transfer(
            handle,
            endpoints->inputEndpoint,
            transferBuffer.data(),
            static_cast<int>(transferBuffer.size()),
            &transferred,
            250);
        if (transferred > 0) {
            streamBuffer.insert(
                streamBuffer.end(),
                transferBuffer.begin(),
                transferBuffer.begin() + transferred);
        }
        if (readResult == LIBUSB_ERROR_TIMEOUT) {
            continue;
        }
        if (readResult == LIBUSB_ERROR_NO_DEVICE) {
            result = RunResult::Disconnected;
            break;
        }
        if (readResult != LIBUSB_SUCCESS) {
            errorHandler("USB receive failed: " + usbError(readResult));
            result = RunResult::Failed;
            break;
        }

        std::size_t consumed = 0;
        while (streamBuffer.size() - consumed >= 4) {
            const auto available = std::span<const std::uint8_t>(streamBuffer).subspan(consumed);
            const auto fullLength = binary::readLe<std::uint32_t>(available);
            if (fullLength < 8 || fullLength > kMaximumFrameBytes) {
                errorHandler("Invalid USB protocol frame length");
                result = RunResult::Failed;
                requestedStop_.store(true);
                break;
            }
            if (available.size() < fullLength) {
                break;
            }
            if (!frameHandler(available.subspan(4, fullLength - 4))) {
                result = RunResult::Failed;
                requestedStop_.store(true);
                break;
            }
            consumed += fullLength;
        }
        if (consumed != 0) {
            streamBuffer.erase(
                streamBuffer.begin(),
                streamBuffer.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
    }

    connected_.store(false);
    {
        std::lock_guard lock(ioMutex_);
        handle_ = nullptr;
        outputEndpoint_ = 0;
        claimedInterface_ = -1;
    }
    libusb_release_interface(handle, endpoints->interfaceNumber);
    libusb_control_transfer(handle, 0x40, 0x52, 0, 0, nullptr, 0, 500);
    if (usbMuxConfiguration_ >= 0) {
        libusb_set_configuration(handle, usbMuxConfiguration_);
    }
    libusb_close(handle);
    libusb_exit(context_);
    context_ = nullptr;
    return result;
}

bool UsbTransport::write(std::span<const std::uint8_t> packet) {
    std::lock_guard lock(ioMutex_);
    if (!handle_ || outputEndpoint_ == 0 || packet.empty()) {
        return false;
    }

    std::size_t offset = 0;
    while (offset < packet.size()) {
        int transferred = 0;
        const auto remaining = packet.size() - offset;
        const auto result = libusb_bulk_transfer(
            handle_,
            outputEndpoint_,
            const_cast<unsigned char*>(packet.data() + offset),
            static_cast<int>(std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            &transferred,
            1000);
        if (result != LIBUSB_SUCCESS || transferred <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(transferred);
    }
    return true;
}

void UsbTransport::requestStop() {
    requestedStop_.store(true);
}

bool UsbTransport::connected() const {
    return connected_.load();
}

void UsbTransport::closeHandle() {
    std::lock_guard lock(ioMutex_);
    if (handle_) {
        if (claimedInterface_ >= 0) {
            libusb_release_interface(handle_, claimedInterface_);
        }
        libusb_close(handle_);
        handle_ = nullptr;
    }
    outputEndpoint_ = 0;
    claimedInterface_ = -1;
    connected_.store(false);
    if (context_) {
        libusb_exit(context_);
        context_ = nullptr;
    }
}

} // namespace padmirror::capture::usb
