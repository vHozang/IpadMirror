#pragma once

#include <QStringList>

#include <functional>

class QObject;

namespace padmirror::app {

class RuntimeDependencies final {
public:
    static void configureEnvironment();
    [[nodiscard]] static QStringList missingComponents();
    [[nodiscard]] static bool runBundledRepair();
    [[nodiscard]] static bool usbCaptureDriverInstalled();
    [[nodiscard]] static bool usbCleanupRestartRequired();
    [[nodiscard]] static bool startBundledUsbDriverInstaller(
        QObject* context,
        std::function<void(bool)> completion);
};

} // namespace padmirror::app
