#pragma once

#include "device/DeviceInfo.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <thread>
#include <vector>

namespace padmirror::device {

class DeviceManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY devicesChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY devicesChanged)
    Q_PROPERTY(QString currentSerial READ currentSerial NOTIFY devicesChanged)
    Q_PROPERTY(bool currentTrusted READ currentTrusted NOTIFY devicesChanged)
    Q_PROPERTY(bool trustKnown READ trustKnown NOTIFY devicesChanged)
    Q_PROPERTY(QString scanError READ scanError NOTIFY devicesChanged)

public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager() override;

    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] bool hasDevice() const;
    [[nodiscard]] QString currentName() const;
    [[nodiscard]] QString currentSerial() const;
    [[nodiscard]] bool currentTrusted() const;
    [[nodiscard]] bool trustKnown() const;
    [[nodiscard]] QString scanError() const;

signals:
    void devicesChanged();

private:
    struct ScanResult {
        std::vector<DeviceInfo> devices;
        QString error;
    };

    static ScanResult scan();
    void applyScan(ScanResult result);

    std::vector<DeviceInfo> devices_;
    QVariantList deviceVariants_;
    QString scanError_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
};

} // namespace padmirror::device
