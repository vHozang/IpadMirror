#include "app/AppController.h"

#include "app/MainWindow.h"
#include "app/RuntimeDependencies.h"
#include "platform/PlatformPower.h"

#include <QMetaObject>
#include <QPointer>

#include <utility>

namespace padmirror::app {

AppController::AppController(QObject* parent)
    : QObject(parent),
      captureSession_(&mediaSession_, this),
      airPlayBackend_(mediaSession_),
      coordinator_(mediaSession_, usbBackend_, airPlayBackend_) {
    connect(&captureSession_, &capture::CaptureSession::stateChanged,
            this, &AppController::handleCaptureState);
    connect(&captureSession_, &capture::CaptureSession::errorOccurred,
            this, &AppController::setError);
    coordinator_.setMediaHandlers(
        [this] { return armMedia(); },
        [this] { mediaSession_.stop(); });
    connect(&settings_, &Settings::connectionModeChanged, this, [this] {
        if (active_) restart();
        else emit stateChanged();
    });
    connect(&settings_, &Settings::airPlayQualityChanged, this, [this] {
        // A quality preference must not interrupt a live session; it applies on
        // the next explicit Apply/start. Refresh the shown label only.
        if (active_ && settings_.connectionMode() == Settings::ConnectionMode::AirPlayWifi) {
            metrics_.setRequestedQuality(airPlayQualityLabel());
        }
        emit stateChanged();
    });
    connect(&settings_, &Settings::alwaysOnTopChanged, this, &AppController::updateWindowFlags);
    // AirPlay has no CaptureSession state machine to drive `streaming_` (that
    // path is USB-only). Instead, watch the 250 ms metrics tick: once decoded
    // video frames are actually flowing, promote the AirPlay session from the
    // Waiting overlay to the live player, and demote it if frames stop.
    connect(&metrics_, &diagnostics::Metrics::changed, this,
            &AppController::updateAirPlayStreaming);
    refreshAudioDevices();
}

AppController::~AppController() {
    stop();
}

void AppController::setMainWindow(MainWindow* window) {
    mainWindow_ = window;
    updateWindowFlags();
}

void AppController::setVideoWindowHandle(std::uintptr_t windowHandle) {
    videoWindowHandle_ = windowHandle;
    mediaSession_.setWindowHandle(windowHandle);
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
        : QStringLiteral("AIRPLAY");
}
QVariantList AppController::audioDevices() const { return audioDevices_; }
bool AppController::wifiAvailable() const {
    return airPlayBackend_.health().available;
}

void AppController::start() {
    stop();
    metrics_.reset();
    errorText_.clear();
    streaming_ = false;
    active_ = true;
    platform::setGamingPowerActive(true);
    if (settings_.connectionMode() == Settings::ConnectionMode::UsbGaming) startUsb();
    else startAirPlay();
    if (!active_) platform::setGamingPowerActive(false);
    emit stateChanged();
}

void AppController::stop() {
    captureSession_.stop();
    // The coordinator owns media teardown (via the registered handler), the
    // active backend stop/shutdown, and the flush/EOS ordering.
    coordinator_.stop();
    platform::setGamingPowerActive(false);
    active_ = false;
    streaming_ = false;
    errorText_.clear();
    statusText_ = QStringLiteral("Ready");
    metrics_.setTransportState(QStringLiteral("Stopped"));
    metrics_.clearAudioBuffer();
    emit stateChanged();
}

void AppController::restart() {
    start();
}

void AppController::applySettings() {
    if (!active_) return;
    restart();
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

void AppController::showStreamingSettings() {
    if (mainWindow_) mainWindow_->showStreamingSettings();
}

void AppController::startUsb() {
    metrics_.setTransport(QStringLiteral("USB (Apple user-mode channel)"));
    metrics_.setTransportState(QStringLiteral("Connecting"));
    metrics_.setDeviceName(QStringLiteral("iPad via USB"));
    metrics_.setRequestedQuality(QStringLiteral("H.264 / PCM 48 kHz"));
    pendingVideoCodec_ = core::VideoCodec::H264;
    pendingAudioCodec_ = core::AudioCodec::PcmS16Le;

    // The coordinator arms media (via armMedia) before starting the backend,
    // so no packet can arrive before the pipeline exists.
    const bool started = coordinator_.startUsb();
    const auto health = coordinator_.health();
    metrics_.setHealthDetail(QString::fromStdString(health.detail));
    if (!started) {
        active_ = false;
        metrics_.setTransportState(QStringLiteral("Unavailable"));
        statusText_ = QStringLiteral("USB capture unavailable");
        setError(QString::fromStdString(health.detail));
        return;
    }
    metrics_.setTransportState(QStringLiteral("Waiting"));
    statusText_ = QStringLiteral("USB ready - connect, unlock, and trust the iPad");
    captureSession_.start(deviceManager_.currentSerial());
}

void AppController::startAirPlay() {
    metrics_.setTransport(QStringLiteral("AirPlay Wi-Fi"));
    metrics_.setTransportState(QStringLiteral("Connecting"));
    metrics_.setDeviceName(QStringLiteral("iPad on local network"));
    metrics_.setRequestedQuality(airPlayQualityLabel());
    pendingVideoCodec_ = core::VideoCodec::H264;
    // AirPlay screen mirroring delivers AAC-ELD audio (compression type ct=8),
    // not raw PCM. Arm the pipeline for AAC-ELD so RaopReceiver's decoded audio
    // packets match the active codec instead of being rejected as a mismatch.
    pendingAudioCodec_ = core::AudioCodec::AacEld;

    const bool started = coordinator_.enableAirPlay();
    const auto health = coordinator_.health();
    metrics_.setHealthDetail(QString::fromStdString(health.detail));
    if (!started) {
        active_ = false;
        metrics_.setTransportState(QStringLiteral("Unavailable"));
        statusText_ = QStringLiteral("AirPlay receiver unavailable");
        setError(QString::fromStdString(health.detail));
        return;
    }
    metrics_.setTransportState(QStringLiteral("Waiting"));
    statusText_ = QStringLiteral("Choose PadMirror-PC in iPad Screen Mirroring");
}

bool AppController::armMedia() {
    return mediaSession_.start(
        settings_, pendingVideoCodec_, pendingAudioCodec_,
        videoWindowHandle_, &metrics_,
        [this](const std::string& message) { publishMediaError(message); });
}

QString AppController::airPlayQualityLabel() const {
    return settings_.airPlayQuality() == Settings::AirPlayQuality::P1080_60
        ? QStringLiteral("1080p target (source cadence preserved)")
        : QStringLiteral("Auto (source cadence preserved)");
}

void AppController::handleCaptureState(capture::CaptureSession::State state) {
    if (!active_ || settings_.connectionMode() != Settings::ConnectionMode::UsbGaming) return;
    switch (state) {
    case capture::CaptureSession::State::Disconnected:
        streaming_ = false;
        statusText_ = QStringLiteral("USB disconnected");
        break;
    case capture::CaptureSession::State::DeviceFound:
        statusText_ = QStringLiteral("iPad detected");
        break;
    case capture::CaptureSession::State::Pairing:
        statusText_ = QStringLiteral("Waiting for iPad trust");
        break;
    case capture::CaptureSession::State::StartingCapture:
        statusText_ = QStringLiteral("Starting Apple-supported USB capture");
        break;
    case capture::CaptureSession::State::ReceiverReady:
        statusText_ = QStringLiteral("USB capture ready - waiting for video");
        break;
    case capture::CaptureSession::State::Streaming:
        streaming_ = true;
        errorText_.clear();
        statusText_ = QStringLiteral("USB streaming");
        break;
    case capture::CaptureSession::State::Recovering:
        statusText_ = QStringLiteral("USB reconnecting");
        break;
    }
    emit stateChanged();
}

void AppController::updateAirPlayStreaming() {
    if (!active_ || settings_.connectionMode() != Settings::ConnectionMode::AirPlayWifi) {
        return;
    }
    // Treat decoded frames as the truth signal: video is decoding and rendering
    // into the D3D11 host window, so the WaitingView should give way. Fall back
    // to false only after the pipeline is torn down (decodedFrames resets to 0
    // on stop()/reset()).
    const bool live = metrics_.decodedFrames() > 0;
    if (live == streaming_) return;
    streaming_ = live;
    if (streaming_) {
        errorText_.clear();
        statusText_ = QStringLiteral("AirPlay streaming");
        metrics_.setTransportState(QStringLiteral("Streaming"));
    } else {
        statusText_ = QStringLiteral("Choose PadMirror-PC in iPad Screen Mirroring");
        metrics_.setTransportState(QStringLiteral("Waiting"));
    }
    emit stateChanged();
}

void AppController::publishMediaError(const std::string& message) {
    const auto text = QString::fromStdString(message);
    QPointer<AppController> guard(this);
    QMetaObject::invokeMethod(this, [guard, text] {
        if (guard) guard->setError(text);
    }, Qt::QueuedConnection);
}

void AppController::setError(const QString& message) {
    errorText_ = message;
    emit stateChanged();
}

void AppController::updateWindowFlags() {
    if (mainWindow_) mainWindow_->setAlwaysOnTop(settings_.alwaysOnTop());
}

} // namespace padmirror::app
