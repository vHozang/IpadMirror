#include "api/AppApiService.h"

#include "app/AppController.h"
#include "app/Settings.h"
#include "device/DeviceManager.h"
#include "diagnostics/Metrics.h"

#include <QVariantMap>

namespace padmirror::api {

namespace {

std::string connectionModeToString(app::Settings::ConnectionMode mode) {
    return mode == app::Settings::ConnectionMode::AirPlayWifi ? "airplay" : "usb";
}

std::string airPlayQualityToString(app::Settings::AirPlayQuality quality) {
    return quality == app::Settings::AirPlayQuality::P1080_60 ? "p1080_60" : "auto";
}

std::string audioModeToString(app::Settings::AudioMode mode) {
    switch (mode) {
        case app::Settings::AudioMode::Exclusive: return "exclusive";
        case app::Settings::AudioMode::Safe: return "safe";
        case app::Settings::AudioMode::LowLatency:
        default: return "lowLatency";
    }
}

bool audioModeFromString(const std::string& value, app::Settings::AudioMode& out) {
    if (value == "lowLatency") { out = app::Settings::AudioMode::LowLatency; return true; }
    if (value == "exclusive") { out = app::Settings::AudioMode::Exclusive; return true; }
    if (value == "safe") { out = app::Settings::AudioMode::Safe; return true; }
    return false;
}

} // namespace

AppApiService::AppApiService(app::AppController& controller,
                             app::Settings& settings,
                             device::DeviceManager& devices,
                             diagnostics::Metrics& metrics)
    : controller_(&controller), settings_(&settings), devices_(&devices), metrics_(&metrics) {}

ApiStatus AppApiService::status() {
    ApiStatus status;
    if (!controller_ || !metrics_) return status;

    // Map the Qt-side transport label to the stable API token.
    const QString transport = metrics_->transport();
    if (transport.contains(QStringLiteral("USB"))) status.transport = "usb";
    else if (transport.contains(QStringLiteral("AirPlay"))) status.transport = "airplay";
    else status.transport = "none";

    status.state = metrics_->transportState().toLower().toStdString();
    status.healthDetail = metrics_->healthDetail().toStdString();
    status.available = controller_->active();
    status.sourceFps = metrics_->sourceFps();
    status.decodeFps = metrics_->decodeFps();
    status.outputFps = metrics_->renderFps();
    status.droppedFrames = metrics_->droppedFrames();
    status.audioBufferMs = metrics_->audioBufferMs();
    status.audioBufferValid = metrics_->audioBufferValid();
    status.activeEpoch = metrics_->activeEpoch();
    status.lastDiscontinuity = metrics_->lastDiscontinuity().toStdString();
    status.videoCodec = metrics_->videoCodec().toStdString();
    status.sourceResolution = metrics_->sourceResolution().toStdString();
    status.requestedQuality = metrics_->requestedQuality().toStdString();
    return status;
}

ApiDevices AppApiService::devices() {
    ApiDevices result;
    if (!devices_) return result;
    result.scanError = devices_->scanError().toStdString();
    const QVariantList list = devices_->devices();
    for (const QVariant& entry : list) {
        const QVariantMap map = entry.toMap();
        ApiDevice device;
        device.name = map.value(QStringLiteral("name")).toString().toStdString();
        device.serial = map.value(QStringLiteral("serial")).toString().toStdString();
        device.trusted = map.value(QStringLiteral("trusted")).toBool();
        device.trustKnown = map.value(QStringLiteral("trustKnown")).toBool();
        result.devices.push_back(std::move(device));
    }
    return result;
}

ApiSettings AppApiService::settings() {
    ApiSettings out;
    if (!settings_) return out;
    out.connectionMode = connectionModeToString(settings_->connectionMode());
    out.airPlayQuality = airPlayQualityToString(settings_->airPlayQuality());
    out.hardwareDecoder = settings_->hardwareDecoder().toStdString();
    out.audioBufferMs = settings_->audioBufferMs();
    out.audioMode = audioModeToString(settings_->audioMode());
    out.audioDevice = settings_->audioDevice().toStdString();
    out.dropStaleFrames = settings_->dropStaleFrames();
    out.gamingMode = settings_->gamingMode();
    out.maintainAspectRatio = settings_->maintainAspectRatio();
    out.fullscreenOnLaunch = settings_->fullscreenOnLaunch();
    out.alwaysOnTop = settings_->alwaysOnTop();
    out.diagnosticsVisible = settings_->diagnosticsVisible();
    return out;
}

ApiCommandResult AppApiService::startUsb() {
    if (!controller_ || !settings_) return ApiCommandResult::failure("service unavailable");
    // Explicit transport selection is routed through the controller, which owns
    // the coordinator; setting the mode then starting keeps USB XOR AirPlay.
    settings_->setConnectionMode(app::Settings::ConnectionMode::UsbGaming);
    controller_->start();
    return ApiCommandResult::success();
}

ApiCommandResult AppApiService::startAirPlay() {
    if (!controller_ || !settings_) return ApiCommandResult::failure("service unavailable");
    settings_->setConnectionMode(app::Settings::ConnectionMode::AirPlayWifi);
    controller_->start();
    return ApiCommandResult::success();
}

ApiCommandResult AppApiService::stop() {
    if (!controller_) return ApiCommandResult::failure("service unavailable");
    controller_->stop();
    return ApiCommandResult::success();
}

ApiCommandResult AppApiService::applyAudioPatch(const AudioPatch& patch) {
    if (!settings_ || !controller_) return ApiCommandResult::failure("service unavailable");
    if (patch.audioBufferMs) {
        const int value = *patch.audioBufferMs;
        if (value < 0 || value > 200) return ApiCommandResult::failure("audioBufferMs out of range");
        settings_->setAudioBufferMs(value);
    }
    if (patch.audioMode) {
        app::Settings::AudioMode mode;
        if (!audioModeFromString(*patch.audioMode, mode)) {
            return ApiCommandResult::failure("unknown audioMode");
        }
        settings_->setAudioMode(mode);
    }
    if (patch.audioDevice) {
        settings_->setAudioDevice(QString::fromStdString(*patch.audioDevice));
    }
    controller_->applySettings();
    return ApiCommandResult::success();
}

ApiCommandResult AppApiService::applyVideoPatch(const VideoPatch& patch) {
    if (!settings_ || !controller_) return ApiCommandResult::failure("service unavailable");
    if (patch.hardwareDecoder) {
        settings_->setHardwareDecoder(QString::fromStdString(*patch.hardwareDecoder));
    }
    if (patch.dropStaleFrames) settings_->setDropStaleFrames(*patch.dropStaleFrames);
    if (patch.gamingMode) settings_->setGamingMode(*patch.gamingMode);
    if (patch.maintainAspectRatio) settings_->setMaintainAspectRatio(*patch.maintainAspectRatio);
    controller_->applySettings();
    return ApiCommandResult::success();
}

ApiCommandResult AppApiService::applyWindowPatch(const WindowPatch& patch) {
    if (!controller_ || !settings_) return ApiCommandResult::failure("service unavailable");
    if (patch.alwaysOnTop) settings_->setAlwaysOnTop(*patch.alwaysOnTop);
    if (patch.diagnosticsVisible) settings_->setDiagnosticsVisible(*patch.diagnosticsVisible);
    if (patch.action) {
        const std::string& action = *patch.action;
        if (action == "fullscreen" || action == "toggleFullscreen") controller_->toggleFullscreen();
        else if (action == "minimize") controller_->minimizeWindow();
        else if (action == "maximize") controller_->toggleMaximize();
        else if (action == "close") controller_->closeWindow();
        else return ApiCommandResult::failure("unknown window action");
    }
    return ApiCommandResult::success();
}

} // namespace padmirror::api
