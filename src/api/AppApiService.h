#pragma once

// Bridges the pure IApiService contract to the live Qt application objects.
//
// The loopback server lives on the UI thread, so its slots (and therefore these
// calls) already run on the UI thread; commands are applied directly. Reads
// pull from Metrics / Settings / DeviceManager snapshots. This adapter is the
// ONLY place the API touches app state, and it exposes no file paths, driver
// actions, UxPlay configuration, or arbitrary command execution.

#include "api/ApiContracts.h"

#include <QPointer>

namespace padmirror::app {
class AppController;
class Settings;
}
namespace padmirror::device {
class DeviceManager;
}
namespace padmirror::diagnostics {
class Metrics;
}

namespace padmirror::api {

class AppApiService final : public IApiService {
public:
    AppApiService(app::AppController& controller,
                  app::Settings& settings,
                  device::DeviceManager& devices,
                  diagnostics::Metrics& metrics);

    ApiStatus status() override;
    ApiDevices devices() override;
    ApiSettings settings() override;

    ApiCommandResult startUsb() override;
    ApiCommandResult startAirPlay() override;
    ApiCommandResult stop() override;

    ApiCommandResult applyAudioPatch(const AudioPatch& patch) override;
    ApiCommandResult applyVideoPatch(const VideoPatch& patch) override;
    ApiCommandResult applyWindowPatch(const WindowPatch& patch) override;

private:
    QPointer<app::AppController> controller_;
    QPointer<app::Settings> settings_;
    QPointer<device::DeviceManager> devices_;
    QPointer<diagnostics::Metrics> metrics_;
};

} // namespace padmirror::api
