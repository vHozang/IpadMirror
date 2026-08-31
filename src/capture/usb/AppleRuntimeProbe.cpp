#include "capture/usb/AppleRuntimeProbe.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace padmirror::capture::usb {

AppleRuntimeProbe::Result AppleRuntimeProbe::probe() {
#ifdef _WIN32
    constexpr wchar_t kAppleMobileDeviceService[] = L"Apple Mobile Device Service";
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        return {.detail = "Apple Devices runtime could not be queried"};
    }
    const auto service = OpenServiceW(manager, kAppleMobileDeviceService, SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(manager);
        return {.detail = "Apple Devices / Apple Mobile Device Service is not installed"};
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD size = 0;
    const bool running = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &size) && status.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    if (!running) {
        return {.detail = "Apple Mobile Device Service is not running"};
    }

    // The public Windows Apple runtime does not expose a documented AV capture
    // channel. This stays false unless a future Apple-supported channel is added.
    return {
        .appleRuntimeAvailable = true,
        .userModeAvChannelAvailable = false,
        .detail = "Apple runtime is available, but no supported user-mode AV channel is exposed",
    };
#else
    return {.detail = "USB capture is supported only on Windows"};
#endif
}

} // namespace padmirror::capture::usb
