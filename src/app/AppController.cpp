#include "app/AppController.h"

#include "app/MainWindow.h"
#include "app/RuntimeDependencies.h"
#include "platform/PlatformPower.h"

#include <QMetaObject>
#include <QPointer>
#include <QDebug>

namespace padmirror::app {

AppController::AppController(QObject* parent)
    : QObject(parent),
      captureSession_(&mediaSession_, this),
      lanReceiver_(this) {
    connect(&captureSession_, &capture::CaptureSession::stateChanged,
            this, &AppController::handleCaptureState);
    connect(&captureSession_, &capture::CaptureSession::errorOccurred,
            this, &AppController::setError);
    connect(&deviceManager_, &device::DeviceManager::devicesChanged, this, [this] {
        if (deviceManager_.hasDevice()) {
            metrics_.setDeviceName(deviceManager_.currentName());
        }
        emit stateChanged();
    });
    connect(&lanReceiver_, &network::LanReceiver::errorOccurred, this, [this](const QString& message) {
        if (settings_.connectionMode() == Settings::ConnectionMode::AirPlayWifi) {
            mediaSession_.stop();
            active_ = false;
            streaming_ = false;
        }
        setError(message);
    });
    connect(&lanReceiver_, &network::LanReceiver::started, this, [this] {
        if (settings_.connectionMode() != Settings::ConnectionMode::AirPlayWifi || !active_) return;
        statusText_ = QStringLiteral("Choose PadMirror in iPad Screen Mirroring");
        emit stateChanged();
    });
    connect(&lanReceiver_, &network::LanReceiver::logLine, this, [](const QString& line) {
        qInfo().noquote() << "UxPlay:" << line;
    });
    connect(&settings_, &Settings::connectionModeChanged, this, [this] {
        if (active_) restart();
        else emit stateChanged();
    });
    connect(&settings_, &Settings::alwaysOnTopChanged, this, &AppController::updateWindowFlags);

    lanMetricsTimer_.setInterval(300);
    connect(&lanMetricsTimer_, &QTimer::timeout, this, [this] {
        if (settings_.connectionMode() != Settings::ConnectionMode::AirPlayWifi || !active_) return;
        const bool nowStreaming = metrics_.sourceFps() > 1.0;
        if (streaming_ != nowStreaming) {
            streaming_ = nowStreaming;
            statusText_ = streaming_ ? QStringLiteral("AirPlay streaming") :
                (lanReceiver_.running()
                    ? QStringLiteral("Choose PadMirror in iPad Screen Mirroring")
                    : QStringLiteral("Preparing AirPlay receiver"));
            emit stateChanged();
        }
    });
    lanMetricsTimer_.start();
    refreshAudioDevices();
}

AppController::~AppController() {
    stop();
}

void AppController::setMainWindow(MainWindow* window) {
    mainWindow_ = window;
    updateWindowFlags();
}

void AppController::setVideoWindowHandle(std::uintptr_t handle) {
    videoWindowHandle_ = handle;
    mediaSession_.setWindowHandle(handle);
}

Settings* AppController::settings() { return &settings_; }
device::DeviceManager* AppController::devices() { return &deviceManager_; }
diagnostics::Metrics* AppController::metrics() { return &metrics_; }
QString AppController::statusText() const { return statusText_; }
QString AppController::errorText() const { return errorText_; }
bool AppController::active() const { return active_; }
bool AppController::streaming() const { return streaming_; }
bool AppController::fullscreen() const { return mainWindow_ && mainWindow_->isFullScreen(); }
QString AppController::connectionLabel() const {
    return settings_.connectionMode() == Settings::ConnectionMode::UsbGaming
        ? QStringLiteral("USB")
        : QStringLiteral("WI-FI");
}
QVariantList AppController::audioDevices() const { return audioDevices_; }
bool AppController::wifiAvailable() const { return !network::LanReceiver::findUxPlay(settings_.uxplayPath()).isEmpty(); }
QStringList AppController::wifiAddresses() const { return network::LanReceiver::localIpv4Addresses(); }

void AppController::start() {
    stop();
    active_ = true;
    errorText_.clear();
    streaming_ = false;
    metrics_.reset();
    platform::setGamingPowerActive(settings_.gamingMode());
    if (settings_.connectionMode() == Settings::ConnectionMode::UsbGaming) startUsb();
    else startWifi();
    emit stateChanged();
}

void AppController::stop() {
    captureSession_.stop();
    lanReceiver_.stop();
    mediaSession_.stop();
    platform::setGamingPowerActive(false);
    active_ = false;
    streaming_ = false;
    if (statusText_ != QStringLiteral("Ready")) statusText_ = QStringLiteral("Ready");
    emit stateChanged();
}

void AppController::restart() {
    start();
}

void AppController::refreshAudioDevices() {
    audioDevices_ = media::MediaSession::audioDevices();
    emit audioDevicesChanged();
}

void AppController::minimizeWindow() {
    if (mainWindow_) mainWindow_->showMinimized();
}

void AppController::toggleMaximize() {
    if (!mainWindow_) return;
    mainWindow_->isMaximized() ? mainWindow_->showNormal() : mainWindow_->showMaximized();
    emit stateChanged();
}

void AppController::closeWindow() {
    if (mainWindow_) mainWindow_->close();
}

void AppController::toggleFullscreen() {
    if (!mainWindow_) return;
    mainWindow_->toggleFullscreen();
    emit stateChanged();
}

void AppController::beginWindowMove() {
    if (mainWindow_) mainWindow_->beginSystemMove();
}

void AppController::startUsb() {
    if (!RuntimeDependencies::usbCaptureDriverInstalled()) {
        if (usbDriverInstallInProgress_) return;
        usbDriverInstallInProgress_ = true;
        statusText_ = QStringLiteral("Installing the signed USB capture driver");
        emit stateChanged();
        const bool started = RuntimeDependencies::startBundledUsbDriverInstaller(this, [this](bool installed) {
            usbDriverInstallInProgress_ = false;
            if (!active_ || settings_.connectionMode() != Settings::ConnectionMode::UsbGaming) return;
            if (!installed) {
                active_ = false;
                setError(QStringLiteral(
                    "USB capture needs the signed UsbDk driver. Approve the Windows administrator prompt or reinstall PadMirror."));
                return;
            }
            statusText_ = QStringLiteral("USB capture driver installed - reconnecting iPad");
            emit stateChanged();
            QTimer::singleShot(250, this, [this] {
                if (active_ && settings_.connectionMode() == Settings::ConnectionMode::UsbGaming) startUsb();
            });
        });
        if (!started) {
            usbDriverInstallInProgress_ = false;
            active_ = false;
            setError(QStringLiteral(
                "USB capture needs the signed UsbDk driver. Approve the Windows administrator prompt or reinstall PadMirror."));
        }
        return;
    }

    metrics_.setTransport(QStringLiteral("USB"));
    metrics_.setDeviceName(deviceManager_.hasDevice()
        ? deviceManager_.currentName()
        : QStringLiteral("iPad"));
    if (!mediaSession_.startUsb(
            settings_, videoWindowHandle_, &metrics_,
            [this](const std::string& message) { publishMediaError(message); })) {
        active_ = false;
        setError(QStringLiteral("Cannot start the USB media pipeline"));
        return;
    }
    statusText_ = deviceManager_.hasDevice()
        ? QStringLiteral("Starting USB capture")
        : QStringLiteral("Connect and unlock your iPad via USB");
    if (deviceManager_.trustKnown() && !deviceManager_.currentTrusted()) {
        statusText_ = QStringLiteral("Unlock the iPad and tap Trust This Computer");
    }
    captureSession_.start(deviceManager_.currentSerial());
}

void AppController::startWifi() {
    metrics_.setTransport(QStringLiteral("AirPlay Wi-Fi"));
    metrics_.setDeviceName(QStringLiteral("iPad on local network"));
    if (!mediaSession_.startLan(
            settings_, kLanVideoPort, kLanAudioPort, videoWindowHandle_, &metrics_,
            [this](const std::string& message) { publishMediaError(message); })) {
        active_ = false;
        setError(QStringLiteral("Cannot start the AirPlay receive pipeline"));
        return;
    }
    if (!lanReceiver_.start(settings_.uxplayPath(), kLanVideoPort, kLanAudioPort)) {
        mediaSession_.stop();
        active_ = false;
        return;
    }
    statusText_ = QStringLiteral("Preparing AirPlay receiver");
}

void AppController::handleCaptureState(capture::CaptureSession::State state) {
    switch (state) {
    case capture::CaptureSession::State::Disconnected:
        streaming_ = false;
        statusText_ = QStringLiteral("Connect and unlock your iPad via USB");
        break;
    case capture::CaptureSession::State::DeviceFound:
        statusText_ = QStringLiteral("iPad detected");
        break;
    case capture::CaptureSession::State::Pairing:
        statusText_ = QStringLiteral("Waiting for iPad trust");
        break;
    case capture::CaptureSession::State::StartingCapture:
        statusText_ = QStringLiteral("Starting USB Gaming Mode");
        break;
    case capture::CaptureSession::State::Streaming:
        streaming_ = true;
        errorText_.clear();
        statusText_ = QStringLiteral("USB Gaming Mode active");
        break;
    case capture::CaptureSession::State::Recovering:
        streaming_ = false;
        statusText_ = QStringLiteral("USB interrupted - reconnecting");
        break;
    }
    emit stateChanged();
}

void AppController::setError(const QString& message) {
    errorText_ = message;
    emit stateChanged();
}

void AppController::publishMediaError(const std::string& message) {
    const auto text = QString::fromStdString(message);
    QPointer<AppController> guard(this);
    QMetaObject::invokeMethod(this, [guard, text] {
        if (guard) guard->setError(text);
    }, Qt::QueuedConnection);
}

void AppController::updateWindowFlags() {
    if (mainWindow_) mainWindow_->setAlwaysOnTop(settings_.alwaysOnTop());
}

} // namespace padmirror::app
