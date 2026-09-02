#include "app/AirPlayController.h"

#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace padmirror::app {

AirPlayController::AirPlayController(ApplicationSettings* settings, QObject* parent)
    : QObject(parent),
      settings_(settings),
      config_(settings_->config()) {
    qRegisterMetaType<airplay::ReceiverState>();
    qRegisterMetaType<airplay::ReceiverDiagnostics>();

    receiver_ = std::make_unique<airplay::UxPlayReceiver>(
        [this](airplay::ReceiverState state, const std::string& detail) {
            const QString text = QString::fromUtf8(
                detail.data(), static_cast<qsizetype>(detail.size()));
            QMetaObject::invokeMethod(
                this,
                [this, state, text] { applyState(state, text); },
                Qt::QueuedConnection);
        },
        [this](const airplay::ReceiverDiagnostics& diagnostics) {
            QMetaObject::invokeMethod(
                this,
                [this, diagnostics] { applyDiagnostics(diagnostics); },
                Qt::QueuedConnection);
        });

    connect(settings_, &ApplicationSettings::configChanged,
            this, &AirPlayController::applyConfig);
}

AirPlayController::~AirPlayController() {
    stop();
}

void AirPlayController::setVideoWindowHandle(std::uintptr_t windowHandle) {
    windowHandle_ = windowHandle;
    receiver_->setWindowHandle(windowHandle_);
}

airplay::ReceiverState AirPlayController::state() const {
    return state_;
}

QString AirPlayController::stateDetail() const {
    return detail_;
}

airplay::ReceiverDiagnostics AirPlayController::diagnostics() const {
    return diagnostics_;
}

bool AirPlayController::running() const {
    return receiver_->running();
}

void AirPlayController::start() {
    if (receiver_->running()) {
        return;
    }
    if (!windowHandle_) {
        applyState(airplay::ReceiverState::Error,
                   QStringLiteral("The video surface is not ready"));
        return;
    }
    const auto result = receiver_->start(config_, windowHandle_);
    if (!result.started) {
        applyState(
            airplay::ReceiverState::Error,
            QString::fromUtf8(result.detail.data(),
                              static_cast<qsizetype>(result.detail.size())));
    }
}

void AirPlayController::stop() {
    if (!receiver_ || !receiver_->running()) {
        return;
    }
    applyState(airplay::ReceiverState::Stopping,
               QStringLiteral("Stopping AirPlay receiver"));
    receiver_->stop();
    applyState(airplay::ReceiverState::Stopping,
               QStringLiteral("AirPlay receiver stopped"));
    updatePowerRequest();
}

void AirPlayController::restart() {
    receiver_->stop();
    start();
    emit receiverRestarted();
}

void AirPlayController::applyConfig(const support::ReceiverConfig& config) {
    const bool requiresRestart = support::receiverRestartRequired(config_, config);
    config_ = config;
    receiver_->setAudioOutput(config_.volumePercent, config_.muted);
    updatePowerRequest();
    if (requiresRestart && receiver_->running()) {
        restart();
    } else if (!receiver_->running() && config_.startReceiverOnLaunch) {
        start();
    }
}

void AirPlayController::applyState(airplay::ReceiverState state,
                                   const QString& detail) {
    const bool beganStreaming =
        state == airplay::ReceiverState::Streaming &&
        state_ != airplay::ReceiverState::Streaming;
    state_ = state;
    detail_ = detail;
    updatePowerRequest();
    emit stateChanged(state_, detail_);
    if (beganStreaming && config_.fullscreenOnConnect) {
        // The window owns presentation mode; the signal is enough for it to react.
    }
}

void AirPlayController::applyDiagnostics(
    const airplay::ReceiverDiagnostics& diagnostics) {
    diagnostics_ = diagnostics;
    emit diagnosticsChanged(diagnostics_);
}

void AirPlayController::updatePowerRequest() {
#ifdef Q_OS_WIN
    const bool keepAwake = config_.keepAwakeWhileStreaming &&
        state_ == airplay::ReceiverState::Streaming;
    SetThreadExecutionState(keepAwake
        ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
        : ES_CONTINUOUS);
#endif
}

} // namespace padmirror::app
