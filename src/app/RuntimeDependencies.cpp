#include "app/RuntimeDependencies.h"

#include <gst/gst.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <cwchar>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <winsvc.h>
#endif

namespace padmirror::app {
namespace {

QString addSearchPath(const QString& first, const QByteArray& current) {
    if (first.isEmpty()) return QString::fromLocal8Bit(current);
    const auto existing = QString::fromLocal8Bit(current);
    if (existing.isEmpty()) return first;
    return first + QDir::listSeparator() + existing;
}

bool hasFactory(const char* name) {
    auto* factory = gst_element_factory_find(name);
    if (!factory) return false;
    gst_object_unref(factory);
    return true;
}

bool hasAnyFactory(std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        if (hasFactory(name)) return true;
    }
    return false;
}

void requireFactory(QStringList& missing, const char* label, std::initializer_list<const char*> names) {
    if (!hasAnyFactory(names)) missing.push_back(QString::fromLatin1(label));
}

#ifdef Q_OS_WIN
std::vector<wchar_t> deviceProperty(
    HDEVINFO deviceSet,
    SP_DEVINFO_DATA& device,
    DWORD property) {
    DWORD requiredBytes = 0;
    DWORD type = 0;
    SetupDiGetDeviceRegistryPropertyW(deviceSet, &device, property, &type, nullptr, 0, &requiredBytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredBytes == 0) return {};
    std::vector<wchar_t> value((requiredBytes / sizeof(wchar_t)) + 1, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceSet,
            &device,
            property,
            &type,
            reinterpret_cast<PBYTE>(value.data()),
            requiredBytes,
            nullptr)) {
        return {};
    }
    return value;
}

bool multiStringContains(const std::vector<wchar_t>& values, const wchar_t* expected) {
    if (values.empty()) return false;
    for (const wchar_t* value = values.data(); value && *value; value += std::wcslen(value) + 1) {
        if (_wcsicmp(value, expected) == 0) return true;
    }
    return false;
}

bool isAppleMobileComposite(
    const std::vector<wchar_t>& hardwareIds,
    const std::vector<wchar_t>& service) {
    if (!multiStringContains(service, L"usbccgp")) return false;
    for (const wchar_t* id = hardwareIds.data(); id && *id; id += std::wcslen(id) + 1) {
        const QString normalized = QString::fromWCharArray(id).toUpper();
        if (normalized.startsWith(QStringLiteral("USB\\VID_05AC&PID_12")) &&
            !normalized.contains(QStringLiteral("&MI_"))) {
            return true;
        }
    }
    return false;
}

bool appleUsbHasUnsafeFilter() {
    const auto deviceSet = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES);
    if (deviceSet == INVALID_HANDLE_VALUE) return false;

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(deviceSet, index, &device)) break;
        const auto hardwareIds = deviceProperty(deviceSet, device, SPDRP_HARDWAREID);
        const auto service = deviceProperty(deviceSet, device, SPDRP_SERVICE);
        if (hardwareIds.empty() || !isAppleMobileComposite(hardwareIds, service)) continue;
        if (multiStringContains(deviceProperty(deviceSet, device, SPDRP_UPPERFILTERS), L"libusb0") ||
            multiStringContains(deviceProperty(deviceSet, device, SPDRP_LOWERFILTERS), L"libusb0")) {
            SetupDiDestroyDeviceInfoList(deviceSet);
            return true;
        }
    }
    SetupDiDestroyDeviceInfoList(deviceSet);
    return false;
}

bool serviceRunning(SC_HANDLE manager, const wchar_t* name) {
    const auto service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS);
    if (!service) return false;
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const bool running = QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded) && status.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(service);
    return running;
}

bool serviceExists(SC_HANDLE manager, const wchar_t* name) {
    const auto service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS);
    if (!service) return false;
    CloseServiceHandle(service);
    return true;
}

bool serviceDisabled(SC_HANDLE manager, const wchar_t* name) {
    const auto service = OpenServiceW(manager, name, SERVICE_QUERY_CONFIG);
    if (!service) return false;
    DWORD requiredBytes = 0;
    QueryServiceConfigW(service, nullptr, 0, &requiredBytes);
    std::vector<std::uint8_t> buffer(requiredBytes);
    const auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    const bool disabled = requiredBytes != 0 && QueryServiceConfigW(
        service,
        reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data()),
        requiredBytes,
        &requiredBytes) && config->dwStartType == SERVICE_DISABLED;
    CloseServiceHandle(service);
    return disabled;
}
#endif

} // namespace

void RuntimeDependencies::configureEnvironment() {
#ifdef Q_OS_WIN
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    const auto pluginDir = applicationDir.filePath(QStringLiteral("gstreamer-1.0"));
    if (QFileInfo::exists(pluginDir)) {
        qputenv(
            "GST_PLUGIN_PATH_1_0",
            addSearchPath(pluginDir, qgetenv("GST_PLUGIN_PATH_1_0")).toLocal8Bit());
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", pluginDir.toLocal8Bit());

        const auto cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheDir.isEmpty() && QDir().mkpath(cacheDir)) {
            const auto executableKey = QCryptographicHash::hash(
                QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toLower().toUtf8(),
                QCryptographicHash::Sha256).toHex().left(12);
            qputenv(
                "GST_REGISTRY_1_0",
                QDir(cacheDir).filePath(
                    QStringLiteral("gstreamer-registry-%1.bin")
                        .arg(QString::fromLatin1(executableKey))).toLocal8Bit());
        }
    }

    QStringList runtimeRoots = {
        applicationDir.absolutePath(),
        qEnvironmentVariable("GSTREAMER_1_0_ROOT_MSVC_X86_64"),
        QStringLiteral("C:/gstreamer/1.0/msvc_x86_64"),
    };
    runtimeRoots.removeAll(QString());

    QString scannerPath;
    QStringList binaryDirs = {QDir::toNativeSeparators(applicationDir.absolutePath())};
    for (const auto& root : runtimeRoots) {
        const QDir runtimeRoot(root);
        const auto bin = runtimeRoot.filePath(QStringLiteral("bin"));
        if (QFileInfo::exists(bin)) binaryDirs.push_back(QDir::toNativeSeparators(bin));
        const auto scanner = runtimeRoot.filePath(
            QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner.exe"));
        if (scannerPath.isEmpty() && QFileInfo::exists(scanner)) scannerPath = scanner;
    }

    auto path = QString::fromLocal8Bit(qgetenv("PATH"));
    for (auto it = binaryDirs.crbegin(); it != binaryDirs.crend(); ++it) {
        path = addSearchPath(*it, path.toLocal8Bit());
    }
    qputenv("PATH", path.toLocal8Bit());
    if (!scannerPath.isEmpty()) {
        qputenv("GST_PLUGIN_SCANNER_1_0", QDir::toNativeSeparators(scannerPath).toLocal8Bit());
    }
#endif
}

QStringList RuntimeDependencies::missingComponents() {
    QStringList missing;
    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        missing.push_back(error
            ? QStringLiteral("GStreamer: %1").arg(QString::fromUtf8(error->message))
            : QStringLiteral("GStreamer runtime"));
        if (error) g_error_free(error);
        return missing;
    }

#ifdef Q_OS_WIN
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    if (!QFileInfo::exists(
            applicationDir.filePath(QStringLiteral("usb-bridge/PadMirrorUsbBridge.exe")))) {
        missing.push_back(QStringLiteral("safe Apple USB bridge"));
    }
    const auto bundledPlugins = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("gstreamer-1.0"));
    if (QFileInfo::exists(bundledPlugins)) {
        const auto pluginPath = QDir::toNativeSeparators(bundledPlugins).toUtf8();
        gst_registry_scan_path(gst_registry_get(), pluginPath.constData());
    }
#endif

    requireFactory(missing, "GStreamer appsrc", {"appsrc"});
    requireFactory(missing, "GStreamer queue", {"queue"});
    requireFactory(missing, "GStreamer H.264 parser", {"h264parse"});
    requireFactory(missing, "GStreamer H.265 parser", {"h265parse"});
    requireFactory(missing, "GStreamer audio converter", {"audioconvert"});
    requireFactory(missing, "GStreamer audio resampler", {"audioresample"});
    requireFactory(missing, "GStreamer UDP source", {"udpsrc"});
    requireFactory(missing, "GStreamer RTP jitter buffer", {"rtpjitterbuffer"});
    requireFactory(missing, "GStreamer H.264 RTP depayloader", {"rtph264depay"});
    requireFactory(missing, "GStreamer L16 RTP depayloader", {"rtpL16depay"});

#ifdef Q_OS_WIN
    requireFactory(missing, "D3D11 H.264 hardware decoder", {"d3d11h264dec"});
    requireFactory(missing, "HEVC decoder", {
        "d3d11h265dec", "d3d11h265device1dec", "nvh265dec", "avdec_h265"});
    requireFactory(missing, "AAC-ELD decoder", {"avdec_aac"});
    requireFactory(missing, "D3D11 video sink", {"d3d11videosink"});
    requireFactory(missing, "WASAPI audio sink", {"wasapi2sink", "wasapisink"});
#elif defined(Q_OS_MACOS)
    requireFactory(missing, "VideoToolbox H.264 decoder", {"vtdec_hw", "vtdec"});
    requireFactory(missing, "CoreAudio audio sink", {"osxaudiosink"});
#endif

    return missing;
}

bool RuntimeDependencies::runBundledRepair() {
#ifdef Q_OS_WIN
    const auto script = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("dependencies/repair-runtime.ps1"));
    if (!QFileInfo::exists(script)) return false;

    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), QDir::toNativeSeparators(script),
        QStringLiteral("-ForceGStreamer"),
    });
    process.start();
    if (!process.waitForStarted(5000)) return false;
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
#else
    return false;
#endif
}

bool RuntimeDependencies::usbCaptureDriverInstalled() {
#ifdef Q_OS_WIN
    if (appleUsbHasUnsafeFilter()) return false;

    const auto bridge = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("usb-bridge/PadMirrorUsbBridge.exe"));
    if (!QFileInfo::exists(bridge)) return false;

    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    const bool unsafeDriverPresent = serviceExists(manager, L"UsbDk");
    const bool ready = !unsafeDriverPresent &&
        serviceRunning(manager, L"Apple Mobile Device Service");
    CloseServiceHandle(manager);
    return ready;
#else
    return true;
#endif
}

bool RuntimeDependencies::usbCleanupRestartRequired() {
#ifdef Q_OS_WIN
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    const bool restartRequired = serviceDisabled(manager, L"UsbDk");
    CloseServiceHandle(manager);
    return restartRequired;
#else
    return false;
#endif
}

bool RuntimeDependencies::startBundledUsbDriverInstaller(
    QObject* context,
    std::function<void(bool)> completion) {
#ifdef Q_OS_WIN
    if (!context || !completion) return false;
    const auto script = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("dependencies/install-usb-capture-driver.ps1"));
    if (!QFileInfo::exists(script)) return false;

    auto* process = new QProcess(context);
    process->setProgram(QStringLiteral("powershell.exe"));
    process->setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), QDir::toNativeSeparators(script),
    });
    const auto completed = std::make_shared<bool>(false);
    auto finish = [process, completed, completion = std::move(completion)](bool success) mutable {
        if (*completed) return;
        *completed = true;
        completion(success);
        process->deleteLater();
    };
    QObject::connect(process, &QProcess::errorOccurred, context,
        [finish](QProcess::ProcessError error) mutable {
            if (error == QProcess::FailedToStart) finish(false);
        });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), context,
        [finish](int exitCode, QProcess::ExitStatus status) mutable {
            finish(status == QProcess::NormalExit && exitCode == 0 &&
                RuntimeDependencies::usbCaptureDriverInstalled());
        });
    process->start();
    return true;
#else
    Q_UNUSED(context)
    completion(true);
    return true;
#endif
}

} // namespace padmirror::app
