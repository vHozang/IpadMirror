#include "app/Settings.h"

#include <QSettings>

#include <algorithm>
#include <cstdlib>

namespace padmirror::app {
namespace {

QSettings store() {
    return QSettings(QStringLiteral("PadMirror"), QStringLiteral("PadMirror"));
}

} // namespace

Settings::Settings(QObject* parent)
    : QObject(parent) {
    load();
}

Settings::ConnectionMode Settings::connectionMode() const { return connectionMode_; }
QString Settings::hardwareDecoder() const { return hardwareDecoder_; }
int Settings::audioBufferMs() const { return audioBufferMs_; }
Settings::AudioMode Settings::audioMode() const { return audioMode_; }
QString Settings::audioDevice() const { return audioDevice_; }
bool Settings::dropStaleFrames() const { return dropStaleFrames_; }
bool Settings::gamingMode() const { return gamingMode_; }
bool Settings::strictSync() const { return strictSync_; }
bool Settings::maintainAspectRatio() const { return maintainAspectRatio_; }
bool Settings::fullscreenOnLaunch() const { return fullscreenOnLaunch_; }
bool Settings::alwaysOnTop() const { return alwaysOnTop_; }
bool Settings::diagnosticsVisible() const { return diagnosticsVisible_; }
QString Settings::uxplayPath() const { return uxplayPath_; }

void Settings::setConnectionMode(ConnectionMode value) {
    if (connectionMode_ == value) return;
    connectionMode_ = value;
    store().setValue(QStringLiteral("connection/mode"), static_cast<int>(value));
    emit connectionModeChanged();
}

void Settings::setHardwareDecoder(const QString& value) {
    if (hardwareDecoder_ == value) return;
    hardwareDecoder_ = value;
    store().setValue(QStringLiteral("video/decoder"), value);
    emit hardwareDecoderChanged();
}

void Settings::setAudioBufferMs(int value) {
    const int allowed[] = {5, 10, 15, 20};
    int normalized = 10;
    for (const auto candidate : allowed) {
        if (std::abs(candidate - value) < std::abs(normalized - value)) normalized = candidate;
    }
    if (audioBufferMs_ == normalized) return;
    audioBufferMs_ = normalized;
    store().setValue(QStringLiteral("audio/bufferMs"), normalized);
    emit audioBufferMsChanged();
}

void Settings::setAudioMode(AudioMode value) {
    if (audioMode_ == value) return;
    audioMode_ = value;
    store().setValue(QStringLiteral("audio/mode"), static_cast<int>(value));
    emit audioModeChanged();
}

void Settings::setAudioDevice(const QString& value) {
    if (audioDevice_ == value) return;
    audioDevice_ = value;
    store().setValue(QStringLiteral("audio/device"), value);
    emit audioDeviceChanged();
}

#define PADMIRROR_BOOL_SETTER(Name, Field, Key, Signal) \
    void Settings::Name(bool value) { \
        if (Field == value) return; \
        Field = value; \
        store().setValue(QStringLiteral(Key), value); \
        emit Signal(); \
    }

PADMIRROR_BOOL_SETTER(setDropStaleFrames, dropStaleFrames_, "video/dropStale", dropStaleFramesChanged)
PADMIRROR_BOOL_SETTER(setGamingMode, gamingMode_, "general/gamingMode", gamingModeChanged)
PADMIRROR_BOOL_SETTER(setStrictSync, strictSync_, "audio/strictSync", strictSyncChanged)
PADMIRROR_BOOL_SETTER(setMaintainAspectRatio, maintainAspectRatio_, "display/aspect", maintainAspectRatioChanged)
PADMIRROR_BOOL_SETTER(setFullscreenOnLaunch, fullscreenOnLaunch_, "display/fullscreen", fullscreenOnLaunchChanged)
PADMIRROR_BOOL_SETTER(setAlwaysOnTop, alwaysOnTop_, "display/alwaysOnTop", alwaysOnTopChanged)
PADMIRROR_BOOL_SETTER(setDiagnosticsVisible, diagnosticsVisible_, "debug/diagnostics", diagnosticsVisibleChanged)

#undef PADMIRROR_BOOL_SETTER

void Settings::setUxplayPath(const QString& value) {
    if (uxplayPath_ == value) return;
    uxplayPath_ = value;
    store().setValue(QStringLiteral("wifi/uxplayPath"), value);
    emit uxplayPathChanged();
}

void Settings::load() {
    auto settings = store();
    connectionMode_ = static_cast<ConnectionMode>(settings.value(
        QStringLiteral("connection/mode"), static_cast<int>(connectionMode_)).toInt());
    hardwareDecoder_ = settings.value(QStringLiteral("video/decoder"), hardwareDecoder_).toString();
    audioBufferMs_ = settings.value(QStringLiteral("audio/bufferMs"), audioBufferMs_).toInt();
    audioMode_ = static_cast<AudioMode>(settings.value(
        QStringLiteral("audio/mode"), static_cast<int>(audioMode_)).toInt());
    audioDevice_ = settings.value(QStringLiteral("audio/device"), audioDevice_).toString();
    dropStaleFrames_ = settings.value(QStringLiteral("video/dropStale"), true).toBool();
    gamingMode_ = settings.value(QStringLiteral("general/gamingMode"), true).toBool();
    strictSync_ = settings.value(QStringLiteral("audio/strictSync"), false).toBool();
    maintainAspectRatio_ = settings.value(QStringLiteral("display/aspect"), true).toBool();
    fullscreenOnLaunch_ = settings.value(QStringLiteral("display/fullscreen"), false).toBool();
    alwaysOnTop_ = settings.value(QStringLiteral("display/alwaysOnTop"), false).toBool();
    diagnosticsVisible_ = settings.value(QStringLiteral("debug/diagnostics"), false).toBool();
    uxplayPath_ = settings.value(QStringLiteral("wifi/uxplayPath")).toString();
    setAudioBufferMs(audioBufferMs_);
}

} // namespace padmirror::app
