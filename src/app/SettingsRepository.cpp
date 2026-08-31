#include "app/SettingsRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace padmirror::app {

QString SettingsRepository::directoryPath() {
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto path = QDir(root).filePath(QStringLiteral("settings"));
    QDir().mkpath(path);
    return path;
}

QJsonObject SettingsRepository::read(const QString& fileName) {
    QFile file(QDir(directoryPath()).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

void SettingsRepository::write(const QString& fileName, const QJsonObject& values) {
    QFile file(QDir(directoryPath()).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(values).toJson(QJsonDocument::Indented));
}

QJsonObject SettingsRepository::app() const { return read(QStringLiteral("app.json")); }
QJsonObject SettingsRepository::usb() const { return read(QStringLiteral("usb.json")); }
QJsonObject SettingsRepository::airPlay() const { return read(QStringLiteral("airplay.json")); }
QJsonObject SettingsRepository::media() const { return read(QStringLiteral("media.json")); }

void SettingsRepository::saveApp(const QJsonObject& values) const { write(QStringLiteral("app.json"), values); }
void SettingsRepository::saveUsb(const QJsonObject& values) const { write(QStringLiteral("usb.json"), values); }
void SettingsRepository::saveAirPlay(const QJsonObject& values) const { write(QStringLiteral("airplay.json"), values); }
void SettingsRepository::saveMedia(const QJsonObject& values) const { write(QStringLiteral("media.json"), values); }

void SettingsRepository::migrateLegacyQSettings() const {
    if (!app().isEmpty() || !media().isEmpty() || !airPlay().isEmpty()) return;

    QSettings legacy(QStringLiteral("PadMirror"), QStringLiteral("PadMirror"));
    saveApp({
        {QStringLiteral("connectionMode"), legacy.value(QStringLiteral("connection/mode"), 0).toInt()},
        {QStringLiteral("fullscreenOnLaunch"), legacy.value(QStringLiteral("display/fullscreen"), false).toBool()},
        {QStringLiteral("alwaysOnTop"), legacy.value(QStringLiteral("display/alwaysOnTop"), false).toBool()},
        {QStringLiteral("diagnosticsVisible"), legacy.value(QStringLiteral("debug/diagnostics"), false).toBool()},
    });
    saveAirPlay({
        {QStringLiteral("quality"), legacy.value(QStringLiteral("video/airPlayQuality"), 0).toInt()},
    });
    saveMedia({
        {QStringLiteral("hardwareDecoder"), legacy.value(QStringLiteral("video/decoder"), QStringLiteral("Auto")).toString()},
        {QStringLiteral("audioBufferMs"), legacy.value(QStringLiteral("audio/bufferMs"), 10).toInt()},
        {QStringLiteral("audioMode"), legacy.value(QStringLiteral("audio/mode"), 0).toInt()},
        {QStringLiteral("audioDevice"), legacy.value(QStringLiteral("audio/device")).toString()},
        {QStringLiteral("dropStaleFrames"), legacy.value(QStringLiteral("video/dropStale"), true).toBool()},
        {QStringLiteral("gamingMode"), legacy.value(QStringLiteral("general/gamingMode"), true).toBool()},
        {QStringLiteral("maintainAspectRatio"), legacy.value(QStringLiteral("display/aspect"), true).toBool()},
    });
}

} // namespace padmirror::app
