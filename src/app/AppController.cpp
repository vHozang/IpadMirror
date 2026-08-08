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
    connect(&captureSession_, &capture::CaptureSession::statusOccurred, this, [this](const QString& message) {
        if (active_ && settings_.connectionMode() == Settings::ConnectionMode::UsbGaming) {
            statusText_ = message;
            emit stateChanged();
        }
    });
    connect(&captureSession_, &capture::CaptureSession::wifiFallbackRequested, this,
        [this](const QString& message) {
            QTimer::singleShot(0, this, [this, message] {
                if (!active_ || settings_.connectionMode() != Settings::ConnectionMode::UsbGaming) return;
                if (!wifiAvailable()) {
                    active_ = false;
                    setError(message + QStringLiteral(" UxPlay is unavailable."));
                    return;
                }
                qWarning().noquote() << message;
                statusText_ = message;
                emit stateChanged();
                settings_.setConnectionMode(Settings::ConnectionMode::AirPlayWifi);
            });
        });
    connect(&deviceManager_, &device::DeviceManager::devicesChanged, this, [this] {
        if (deviceManager_.hasDevice()) {
            metrics_.setDeviceName(deviceManager_.currentName());
        }
        if (active_ && settings_.connectionMode() == Settings::ConnectionMode::UsbGaming &&
            deviceManager_.hasDevice() && !usbDriverInstallInProgress_ &&
            (!RuntimeDependencies::usbCaptureDriverInstalled() || !captureSession_.running())) {
            captureSession_.stop();
            mediaSession_.stop();
            startUsb();
            return;
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
    connect(&lanReceiver_, &network::LanReceiver::logLine, this, [this](const QString& line) {
        qInfo().noquote() << "UxPlay:" << line;
        handleLanLogLine(line);
    });
    connect(&settings_, &Settings::connectionModeChanged, this, [this] {
        if (active_) restart();
        else emit stateChanged();
    });
    connect(&settings_, &Settings::alwaysOnTopChanged, this, &AppController::updateWindowFlags);

    lanMetricsTimer_.setInterval(300);
    connect(&lanMetricsTimer_, &QTimer::timeout, this, [this] {
        if (settings_.connectionMode() != Settings::ConnectionMode::AirPlayWifi || !active_) return;
        if (!streaming_ && metrics_.sourceFps() > 0.0) {
            streaming_ = true;
            statusText_ = QStringLiteral("AirPlay streaming");
            emit stateChanged();
            return;
        }
        if (streaming_) {
            const auto nextStatus = metrics_.sourceFps() > 1.0
                ? QStringLiteral("AirPlay streaming")
                : QStringLiteral("AirPlay connected - screen idle");
            if (statusText_ != nextStatus) {
                statusText_ = nextStatus;
                emit stateChanged();
            }
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
    if (RuntimeDependencies::usbCleanupRestartRequired()) {
        active_ = false;
        setError(QStringLiteral(
            "UsbDk was disabled and is pending removal. Restart Windows once before using USB mode."));
        return;
    }
    if (!RuntimeDependencies::usbCaptureDriverInstalled()) {
        if (usbDriverInstallInProgress_) return;
        usbDriverInstallInProgress_ = true;
        statusText_ = QStringLiteral("Preparing safe Apple USB support");
        emit stateChanged();
        const bool started = RuntimeDependencies::startBundledUsbDriverInstaller(this, [this](bool installed) {
            usbDriverInstallInProgress_ = false;
            if (!active_ || settings_.connectionMode() != Settings::ConnectionMode::UsbGaming) return;
            if (!installed) {
                active_ = false;
                if (RuntimeDependencies::usbCleanupRestartRequired()) {
                    setError(QStringLiteral(
                        "UsbDk was disabled and is pending removal. Restart Windows once before using USB mode."));
                    return;
                }
                setError(QStringLiteral(
                    "USB needs Apple Devices and removal of unsafe UsbDk/libusb0 drivers. Approve the administrator prompt, then reconnect the iPad."));
                return;
            }
            statusText_ = QStringLiteral("Safe Apple USB support is ready - reconnecting iPad");
            emit stateChanged();
            QTimer::singleShot(250, this, [this] {
                if (active_ && settings_.connectionMode() == Settings::ConnectionMode::UsbGaming) startUsb();
            });
        });
        if (!started) {
            usbDriverInstallInProgress_ = false;
            active_ = false;
            setError(QStringLiteral(
                "USB needs Apple Devices and removal of unsafe UsbDk/libusb0 drivers. Approve the administrator prompt, then reconnect the iPad."));
        }
        return;
    }

    metrics_.setTransport(QStringLiteral("USB"));
    metrics_.setDeviceName(deviceManager_.hasDevice()
        ? deviceManager_.currentName()
        : QStringLiteral("iPad"));
    if (!mediaSession_.startUsb(
            settings_,
#ifdef Q_OS_WIN
            capture::VideoCodec::Hevc,
            capture::AudioCodec::AacEld,
#else
            capture::VideoCodec::H264,
            capture::AudioCodec::PcmS16Le,
#endif
            videoWindowHandle_, &metrics_,
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
    if (settings_.connectionMode() != Settings::ConnectionMode::UsbGaming) return;
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

void AppController::handleLanLogLine(const QString& line) {
    if (settings_.connectionMode() != Settings::ConnectionMode::AirPlayWifi || !active_) return;

    if (line.contains(QStringLiteral("Begin streaming to GStreamer video pipeline"),
                      Qt::CaseInsensitive)) {
        streaming_ = true;
        errorText_.clear();
        statusText_ = QStringLiteral("AirPlay streaming");
        emit stateChanged();
        return;
    }
    if (line.contains(QStringLiteral("raop_rtp_mirror->running is no longer true"),
                      Qt::CaseInsensitive)) {
        streaming_ = false;
        metrics_.reset();
        statusText_ = lanReceiver_.running()
            ? QStringLiteral("Choose PadMirror in iPad Screen Mirroring")
            : QStringLiteral("Preparing AirPlay receiver");
        emit stateChanged();
        return;
    }
    if (!streaming_ && line.contains(QStringLiteral("Accepted IPv4 client"),
                                     Qt::CaseInsensitive)) {
        statusText_ = QStringLiteral("iPad connected - waiting for video");
        emit stateChanged();
    }
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
