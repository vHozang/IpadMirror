#pragma once

#include <QJsonObject>
#include <QString>

namespace padmirror::app {

class SettingsRepository final {
public:
    [[nodiscard]] QJsonObject app() const;
    [[nodiscard]] QJsonObject usb() const;
    [[nodiscard]] QJsonObject airPlay() const;
    [[nodiscard]] QJsonObject media() const;

    void saveApp(const QJsonObject& values) const;
    void saveUsb(const QJsonObject& values) const;
    void saveAirPlay(const QJsonObject& values) const;
    void saveMedia(const QJsonObject& values) const;

    void migrateLegacyQSettings() const;

private:
    [[nodiscard]] static QString directoryPath();
    [[nodiscard]] static QJsonObject read(const QString& fileName);
    static void write(const QString& fileName, const QJsonObject& values);
};

} // namespace padmirror::app
