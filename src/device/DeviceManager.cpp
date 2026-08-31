#include "device/DeviceManager.h"

#include <QMetaObject>
#include <QPointer>

#include <chrono>
#include <thread>

namespace padmirror::device {

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent) {
    worker_ = std::thread([this] {
        while (!stopRequested_.load()) {
            auto result = scan();
            QPointer<DeviceManager> guard(this);
            QMetaObject::invokeMethod(this, [guard, result = std::move(result)]() mutable {
                if (guard) guard->applyScan(std::move(result));
            }, Qt::QueuedConnection);

            for (int tick = 0; tick < 15 && !stopRequested_.load(); ++tick) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

DeviceManager::~DeviceManager() {
    stopRequested_.store(true);
    if (worker_.joinable()) worker_.join();
}

QVariantList DeviceManager::devices() const { return deviceVariants_; }
bool DeviceManager::hasDevice() const { return !devices_.empty(); }
QString DeviceManager::currentName() const { return devices_.empty() ? QString{} : devices_.front().name; }
QString DeviceManager::currentSerial() const { return devices_.empty() ? QString{} : devices_.front().usbSerial; }
bool DeviceManager::currentTrusted() const { return !devices_.empty() && devices_.front().trusted; }
bool DeviceManager::trustKnown() const { return !devices_.empty() && devices_.front().trustKnown; }
QString DeviceManager::scanError() const { return scanError_; }

DeviceManager::ScanResult DeviceManager::scan() {
    return {.error = QStringLiteral("USB_CAPTURE_UNSUPPORTED: Apple runtime AV channel is unavailable")};
}

void DeviceManager::applyScan(ScanResult result) {
    const bool changed = result.error != scanError_ || result.devices.size() != devices_.size();
    if (!changed) return;

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
