#pragma once

#include <QObject>
#include <QString>

namespace padmirror::app {

class Settings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ConnectionMode connectionMode READ connectionMode WRITE setConnectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString hardwareDecoder READ hardwareDecoder WRITE setHardwareDecoder NOTIFY hardwareDecoderChanged)
    Q_PROPERTY(int audioBufferMs READ audioBufferMs WRITE setAudioBufferMs NOTIFY audioBufferMsChanged)
    Q_PROPERTY(AudioMode audioMode READ audioMode WRITE setAudioMode NOTIFY audioModeChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(bool dropStaleFrames READ dropStaleFrames WRITE setDropStaleFrames NOTIFY dropStaleFramesChanged)
    Q_PROPERTY(bool gamingMode READ gamingMode WRITE setGamingMode NOTIFY gamingModeChanged)
    Q_PROPERTY(bool strictSync READ strictSync WRITE setStrictSync NOTIFY strictSyncChanged)
    Q_PROPERTY(bool maintainAspectRatio READ maintainAspectRatio WRITE setMaintainAspectRatio NOTIFY maintainAspectRatioChanged)
    Q_PROPERTY(bool fullscreenOnLaunch READ fullscreenOnLaunch WRITE setFullscreenOnLaunch NOTIFY fullscreenOnLaunchChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(bool diagnosticsVisible READ diagnosticsVisible WRITE setDiagnosticsVisible NOTIFY diagnosticsVisibleChanged)
    Q_PROPERTY(QString uxplayPath READ uxplayPath WRITE setUxplayPath NOTIFY uxplayPathChanged)

public:
    enum class ConnectionMode {
        UsbGaming,
        AirPlayWifi,
    };
    Q_ENUM(ConnectionMode)

    enum class AudioMode {
        LowLatency,
        Exclusive,
        Safe,
    };
    Q_ENUM(AudioMode)

    explicit Settings(QObject* parent = nullptr);

    [[nodiscard]] ConnectionMode connectionMode() const;
    [[nodiscard]] QString hardwareDecoder() const;
    [[nodiscard]] int audioBufferMs() const;
    [[nodiscard]] AudioMode audioMode() const;
    [[nodiscard]] QString audioDevice() const;
    [[nodiscard]] bool dropStaleFrames() const;
    [[nodiscard]] bool gamingMode() const;
    [[nodiscard]] bool strictSync() const;
    [[nodiscard]] bool maintainAspectRatio() const;
    [[nodiscard]] bool fullscreenOnLaunch() const;
    [[nodiscard]] bool alwaysOnTop() const;
    [[nodiscard]] bool diagnosticsVisible() const;
    [[nodiscard]] QString uxplayPath() const;

public slots:
    void setConnectionMode(ConnectionMode value);
    void setHardwareDecoder(const QString& value);
    void setAudioBufferMs(int value);
    void setAudioMode(AudioMode value);
    void setAudioDevice(const QString& value);
    void setDropStaleFrames(bool value);
    void setGamingMode(bool value);
    void setStrictSync(bool value);
    void setMaintainAspectRatio(bool value);
    void setFullscreenOnLaunch(bool value);
    void setAlwaysOnTop(bool value);
    void setDiagnosticsVisible(bool value);
    void setUxplayPath(const QString& value);

signals:
    void connectionModeChanged();
    void hardwareDecoderChanged();
    void audioBufferMsChanged();
    void audioModeChanged();
    void audioDeviceChanged();
    void dropStaleFramesChanged();
    void gamingModeChanged();
    void strictSyncChanged();
    void maintainAspectRatioChanged();
    void fullscreenOnLaunchChanged();
    void alwaysOnTopChanged();
    void diagnosticsVisibleChanged();
    void uxplayPathChanged();

private:
    void load();

    ConnectionMode connectionMode_ = ConnectionMode::UsbGaming;
    QString hardwareDecoder_ = QStringLiteral("Auto");
    int audioBufferMs_ = 10;
    AudioMode audioMode_ = AudioMode::LowLatency;
    QString audioDevice_;
    bool dropStaleFrames_ = true;
    bool gamingMode_ = true;
    bool strictSync_ = false;
    bool maintainAspectRatio_ = true;
    bool fullscreenOnLaunch_ = false;
    bool alwaysOnTop_ = false;
    bool diagnosticsVisible_ = false;
    QString uxplayPath_;
};

} // namespace padmirror::app
