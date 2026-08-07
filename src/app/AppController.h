#pragma once

#include "app/Settings.h"
#include "capture/CaptureSession.h"
#include "device/DeviceManager.h"
#include "diagnostics/Metrics.h"
#include "media/MediaSession.h"
#include "network/LanReceiver.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <cstdint>

namespace padmirror::app {

class MainWindow;

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY stateChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY stateChanged)
    Q_PROPERTY(QString connectionLabel READ connectionLabel NOTIFY stateChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY stateChanged)
    Q_PROPERTY(QStringList wifiAddresses READ wifiAddresses CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    void setMainWindow(MainWindow* window);
    void setVideoWindowHandle(std::uintptr_t handle);

    [[nodiscard]] Settings* settings();
    [[nodiscard]] device::DeviceManager* devices();
    [[nodiscard]] diagnostics::Metrics* metrics();
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool streaming() const;
    [[nodiscard]] bool fullscreen() const;
    [[nodiscard]] QString connectionLabel() const;
    [[nodiscard]] QVariantList audioDevices() const;
    [[nodiscard]] bool wifiAvailable() const;
    [[nodiscard]] QStringList wifiAddresses() const;

public slots:
    void start();
    void stop();
    void restart();
    void refreshAudioDevices();
    void minimizeWindow();
    void toggleMaximize();
    void closeWindow();
    void toggleFullscreen();
    void beginWindowMove();

signals:
    void stateChanged();
    void audioDevicesChanged();

private:
    void startUsb();
    void startWifi();
    void handleCaptureState(capture::CaptureSession::State state);
    void setError(const QString& message);
    void publishMediaError(const std::string& message);
    void updateWindowFlags();

    Settings settings_;
    device::DeviceManager deviceManager_;
    diagnostics::Metrics metrics_;
    media::MediaSession mediaSession_;
    capture::CaptureSession captureSession_;
    network::LanReceiver lanReceiver_;
    MainWindow* mainWindow_ = nullptr;
    std::uintptr_t videoWindowHandle_ = 0;
    QString statusText_ = QStringLiteral("Ready");
    QString errorText_;
    QVariantList audioDevices_;
    bool active_ = false;
    bool streaming_ = false;
    bool usbDriverInstallInProgress_ = false;
    QTimer lanMetricsTimer_;
    static constexpr std::uint16_t kLanVideoPort = 50100;
    static constexpr std::uint16_t kLanAudioPort = 50102;
};

} // namespace padmirror::app
