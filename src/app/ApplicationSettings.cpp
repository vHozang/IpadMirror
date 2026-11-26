#include "app/ApplicationSettings.h"

#include <QSettings>

namespace padmirror::app {
namespace {

support::ReceiverConfig loadConfig() {
    QSettings settings;
    support::ReceiverConfig config;
    config.receiverName = settings.value(QStringLiteral("receiver/name"),
                                         QStringLiteral("PadMirror"))
                              .toString()
                              .toUtf8()
                              .toStdString();
    config.maxFps = settings.value(QStringLiteral("receiver/maxFps"), 60).toInt();
    config.qualityProfile = static_cast<support::QualityProfile>(
        settings.value(QStringLiteral("receiver/qualityProfile"), 1).toInt());
    config.bindAddress = settings.value(QStringLiteral("network/bindAddress"), QString())
                             .toString()
                             .trimmed()
                             .toUtf8()
                             .toStdString();
    config.volumePercent = settings.value(QStringLiteral("audio/volumePercent"), 100).toInt();
    config.muted = settings.value(QStringLiteral("audio/muted"), false).toBool();
    config.keepAwakeWhileStreaming =
        settings.value(QStringLiteral("behavior/keepAwakeWhileStreaming"), true).toBool();
    config.startReceiverOnLaunch =
        settings.value(QStringLiteral("behavior/startReceiverOnLaunch"), true).toBool();
    config.fullscreenOnConnect =
        settings.value(QStringLiteral("behavior/fullscreenOnConnect"), false).toBool();
    return support::normalizeReceiverConfig(config);
}

void saveConfig(const support::ReceiverConfig& config) {
    QSettings settings;
    settings.setValue(
        QStringLiteral("receiver/name"),
        QString::fromUtf8(config.receiverName.data(),
                          static_cast<qsizetype>(config.receiverName.size())));
    settings.setValue(QStringLiteral("receiver/maxFps"), config.maxFps);
    settings.setValue(QStringLiteral("receiver/qualityProfile"),
                      static_cast<int>(config.qualityProfile));
    settings.setValue(QStringLiteral("network/bindAddress"),
                      QString::fromStdString(config.bindAddress));
    settings.setValue(QStringLiteral("audio/volumePercent"), config.volumePercent);
    settings.setValue(QStringLiteral("audio/muted"), config.muted);
    settings.setValue(QStringLiteral("behavior/keepAwakeWhileStreaming"),
                      config.keepAwakeWhileStreaming);
    settings.setValue(QStringLiteral("behavior/startReceiverOnLaunch"),
                      config.startReceiverOnLaunch);
    settings.setValue(QStringLiteral("behavior/fullscreenOnConnect"),
                      config.fullscreenOnConnect);
    settings.sync();
}

} // namespace

ApplicationSettings::ApplicationSettings(QObject* parent)
    : QObject(parent),
      config_(loadConfig()) {
    qRegisterMetaType<support::ReceiverConfig>();
}

support::ReceiverConfig ApplicationSettings::config() const {
    return config_;
}

void ApplicationSettings::setConfig(const support::ReceiverConfig& requestedConfig) {
    const auto config = support::normalizeReceiverConfig(requestedConfig);
    if (config == config_) {
        return;
    }
    config_ = config;
    saveConfig(config_);
    emit configChanged(config_);
}

} // namespace padmirror::app
