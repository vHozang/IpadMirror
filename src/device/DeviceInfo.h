#pragma once

#include <QString>

namespace padmirror::device {

struct DeviceInfo {
    QString udid;
    QString usbSerial;
    QString name;
    QString osVersion;
    bool trusted = false;
    bool trustKnown = false;
};

} // namespace padmirror::device
