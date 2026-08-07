#include "device/DeviceManager.h"

#include "capture/usb/UsbTransport.h"

#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>

#ifdef PADMIRROR_HAVE_IMOBILEDEVICE
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace padmirror::device {
namespace {

QString normalizeSerial(QString value) {
    value.remove(QLatin1Char('-'));
    return value.toLower();
}

bool sameDevice(const DeviceInfo& left, const DeviceInfo& right) {
    return left.udid == right.udid && left.usbSerial == right.usbSerial &&
        left.name == right.name && left.osVersion == right.osVersion &&
        left.trusted == right.trusted && left.trustKnown == right.trustKnown;
}

} // namespace

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent) {
    worker_ = std::thread([this] {
          while (!stopRequested_.load()) {
              auto result = scan();
              QPointer<DeviceManager> guard(this);
              QMetaObject::invokeMethod(this, [guard, result = std::move(result)]() mutable {
                  if (guard) {
                      guard->applyScan(std::move(result));
                  }
              }, Qt::QueuedConnection);

              for (int tick = 0; tick < 15 && !stopRequested_.load(); ++tick) {
                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
              }
          }
      });
}

DeviceManager::~DeviceManager() {
    stopRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

QVariantList DeviceManager::devices() const { return deviceVariants_; }
bool DeviceManager::hasDevice() const { return !devices_.empty(); }
QString DeviceManager::currentName() const { return devices_.empty() ? QString{} : devices_.front().name; }
QString DeviceManager::currentSerial() const {
    if (devices_.empty()) return {};
    return devices_.front().usbSerial.isEmpty() ? devices_.front().udid : devices_.front().usbSerial;
}
bool DeviceManager::currentTrusted() const { return !devices_.empty() && devices_.front().trusted; }
bool DeviceManager::trustKnown() const { return !devices_.empty() && devices_.front().trustKnown; }
QString DeviceManager::scanError() const { return scanError_; }

DeviceManager::ScanResult DeviceManager::scan() {
    ScanResult result;
    std::string usbError;
    const auto usbDevices = capture::usb::UsbTransport::enumerate(usbError);
    for (const auto& usb : usbDevices) {
        DeviceInfo info;
        info.usbSerial = QString::fromStdString(usb.serial);
        info.udid = info.usbSerial;
        info.name = QString::fromStdString(usb.productName);
        if (info.name.isEmpty()) info.name = QStringLiteral("iPad");
        result.devices.push_back(std::move(info));
    }
    if (!usbError.empty()) {
        result.error = QString::fromStdString(usbError);
    }

#ifdef PADMIRROR_HAVE_IMOBILEDEVICE
    idevice_info_t* mobileDevices = nullptr;
    int count = 0;
    if (idevice_get_device_list_extended(&mobileDevices, &count) == IDEVICE_E_SUCCESS) {
        for (int index = 0; index < count; ++index) {
            const auto* mobile = mobileDevices[index];
            if (!mobile || mobile->conn_type != CONNECTION_USBMUXD || !mobile->udid) {
                continue;
            }
            const auto udid = QString::fromUtf8(mobile->udid);
            auto found = std::find_if(result.devices.begin(), result.devices.end(), [&udid](const DeviceInfo& item) {
                return normalizeSerial(item.usbSerial) == normalizeSerial(udid);
            });
            if (found == result.devices.end()) {
                result.devices.push_back(DeviceInfo{.udid = udid, .usbSerial = udid, .name = QStringLiteral("iPad")});
                found = std::prev(result.devices.end());
            }
            found->udid = udid;
            found->trustKnown = true;

            idevice_t device = nullptr;
            if (idevice_new_with_options(&device, mobile->udid, IDEVICE_LOOKUP_USBMUX) != IDEVICE_E_SUCCESS) {
                found->trusted = false;
                continue;
            }
            lockdownd_client_t lockdown = nullptr;
            const auto handshake = lockdownd_client_new_with_handshake(device, &lockdown, "PadMirror");
            found->trusted = handshake == LOCKDOWN_E_SUCCESS;
            if (lockdown) {
                char* deviceName = nullptr;
                if (lockdownd_get_device_name(lockdown, &deviceName) == LOCKDOWN_E_SUCCESS && deviceName) {
                    found->name = QString::fromUtf8(deviceName);
                    std::free(deviceName);
                }
                lockdownd_client_free(lockdown);
            }
            idevice_free(device);
        }
        idevice_device_list_extended_free(mobileDevices);
    }
#endif

    return result;
}

void DeviceManager::applyScan(ScanResult result) {
    bool changed = result.error != scanError_ || result.devices.size() != devices_.size();
    if (!changed) {
        for (std::size_t index = 0; index < devices_.size(); ++index) {
            if (!sameDevice(devices_[index], result.devices[index])) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        return;
    }

    devices_ = std::move(result.devices);
    scanError_ = std::move(result.error);
    deviceVariants_.clear();
    for (const auto& device : devices_) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), device.name);
        map.insert(QStringLiteral("udid"), device.udid);
        map.insert(QStringLiteral("serial"), device.usbSerial);
        map.insert(QStringLiteral("trusted"), device.trusted);
        map.insert(QStringLiteral("trustKnown"), device.trustKnown);
        deviceVariants_.push_back(map);
    }
    emit devicesChanged();
}

} // namespace padmirror::device
