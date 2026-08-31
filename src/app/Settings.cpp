#include "app/Settings.h"

#include "app/SettingsRepository.h"

#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cstdlib>

namespace padmirror::app {
namespace {

SettingsRepository& repository() {
    static SettingsRepository instance;
    return instance;
}

template <typename Enum>
Enum enumValue(const QJsonObject& object, const char* key, Enum fallback, int maximum) {
    const auto value = object.value(QLatin1String(key)).toInt(static_cast<int>(fallback));
    return value >= 0 && value <= maximum ? static_cast<Enum>(value) : fallback;
}

} // namespace

Settings::Settings(QObject* parent)
    : QObject(parent) {
    repository().migrateLegacyQSettings();
    load();
}

Settings::ConnectionMode Settings::connectionMode() const { return connectionMode_; }
Settings::AirPlayQuality Settings::airPlayQuality() const { return airPlayQuality_; }
QString Settings::hardwareDecoder() const { return hardwareDecoder_; }
int Settings::audioBufferMs() const { return audioBufferMs_; }
Settings::AudioMode Settings::audioMode() const { return audioMode_; }
QString Settings::audioDevice() const { return audioDevice_; }
bool Settings::dropStaleFrames() const { return dropStaleFrames_; }
bool Settings::gamingMode() const { return gamingMode_; }
bool Settings::maintainAspectRatio() const { return maintainAspectRatio_; }
bool Settings::fullscreenOnLaunch() const { return fullscreenOnLaunch_; }
bool Settings::alwaysOnTop() const { return alwaysOnTop_; }
bool Settings::diagnosticsVisible() const { return diagnosticsVisible_; }

void Settings::setConnectionMode(ConnectionMode value) {
    if (connectionMode_ == value) return;
    connectionMode_ = value;
    saveApp();
    emit connectionModeChanged();
}

void Settings::setAirPlayQuality(AirPlayQuality value) {
    if (airPlayQuality_ == value) return;
    airPlayQuality_ = value;
    saveAirPlay();
    emit airPlayQualityChanged();
}

void Settings::setHardwareDecoder(const QString& value) {
    if (hardwareDecoder_ == value) return;
    hardwareDecoder_ = value;
    saveMedia();
    emit hardwareDecoderChanged();
}

void Settings::setAudioBufferMs(int value) {
    constexpr std::array allowed = {5, 8, 10, 20, 35};
    auto normalized = allowed.front();
    for (const auto candidate : allowed) {
        if (std::abs(candidate - value) < std::abs(normalized - value)) normalized = candidate;
    }
    if (audioBufferMs_ == normalized) return;
    audioBufferMs_ = normalized;
    saveMedia();
    emit audioBufferMsChanged();
}

void Settings::setAudioMode(AudioMode value) {
    if (audioMode_ == value) return;
    audioMode_ = value;
    saveMedia();
    emit audioModeChanged();
}

void Settings::setAudioDevice(const QString& value) {
    if (audioDevice_ == value) return;
    audioDevice_ = value;
    saveMedia();
    emit audioDeviceChanged();
}

#define PADMIRROR_BOOL_SETTER(Name, Field, Save, Signal) \
    void Settings::Name(bool value) { \
        if (Field == value) return; \
        Field = value; \
        Save(); \
        emit Signal(); \
    }

PADMIRROR_BOOL_SETTER(setDropStaleFrames, dropStaleFrames_, saveMedia, dropStaleFramesChanged)
PADMIRROR_BOOL_SETTER(setGamingMode, gamingMode_, saveMedia, gamingModeChanged)
PADMIRROR_BOOL_SETTER(setMaintainAspectRatio, maintainAspectRatio_, saveMedia, maintainAspectRatioChanged)
PADMIRROR_BOOL_SETTER(setFullscreenOnLaunch, fullscreenOnLaunch_, saveApp, fullscreenOnLaunchChanged)
PADMIRROR_BOOL_SETTER(setAlwaysOnTop, alwaysOnTop_, saveApp, alwaysOnTopChanged)
PADMIRROR_BOOL_SETTER(setDiagnosticsVisible, diagnosticsVisible_, saveApp, diagnosticsVisibleChanged)

#undef PADMIRROR_BOOL_SETTER

void Settings::load() {
    const auto app = repository().app();
    const auto airPlay = repository().airPlay();
    const auto media = repository().media();
    connectionMode_ = enumValue(app, "connectionMode", ConnectionMode::UsbGaming, 1);
    airPlayQuality_ = enumValue(airPlay, "quality", AirPlayQuality::Auto, 1);
    hardwareDecoder_ = media.value(QStringLiteral("hardwareDecoder")).toString(hardwareDecoder_);
    audioBufferMs_ = media.value(QStringLiteral("audioBufferMs")).toInt(audioBufferMs_);
    audioMode_ = enumValue(media, "audioMode", AudioMode::LowLatency, 2);
    audioDevice_ = media.value(QStringLiteral("audioDevice")).toString();
    dropStaleFrames_ = media.value(QStringLiteral("dropStaleFrames")).toBool(true);
    gamingMode_ = media.value(QStringLiteral("gamingMode")).toBool(true);
    maintainAspectRatio_ = media.value(QStringLiteral("maintainAspectRatio")).toBool(true);
    fullscreenOnLaunch_ = app.value(QStringLiteral("fullscreenOnLaunch")).toBool(false);
    alwaysOnTop_ = app.value(QStringLiteral("alwaysOnTop")).toBool(false);
    diagnosticsVisible_ = app.value(QStringLiteral("diagnosticsVisible")).toBool(false);
    setAudioBufferMs(audioBufferMs_);
}

void Settings::saveApp() const {
    repository().saveApp({
        {QStringLiteral("connectionMode"), static_cast<int>(connectionMode_)},
        {QStringLiteral("fullscreenOnLaunch"), fullscreenOnLaunch_},
        {QStringLiteral("alwaysOnTop"), alwaysOnTop_},
        {QStringLiteral("diagnosticsVisible"), diagnosticsVisible_},
    });
}

void Settings::saveAirPlay() const {
    repository().saveAirPlay({
        {QStringLiteral("quality"), static_cast<int>(airPlayQuality_)},
    });
}

void Settings::saveMedia() const {
    repository().saveMedia({
        {QStringLiteral("hardwareDecoder"), hardwareDecoder_},
        {QStringLiteral("audioBufferMs"), audioBufferMs_},
        {QStringLiteral("audioMode"), static_cast<int>(audioMode_)},
        {QStringLiteral("audioDevice"), audioDevice_},
        {QStringLiteral("dropStaleFrames"), dropStaleFrames_},
        {QStringLiteral("gamingMode"), gamingMode_},
        {QStringLiteral("maintainAspectRatio"), maintainAspectRatio_},
    });
}

} // namespace padmirror::app
