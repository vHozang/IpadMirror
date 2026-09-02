#pragma once

#include "support/ReceiverConfig.h"

#include <QObject>
#include <QMetaType>

namespace padmirror::app {

class ApplicationSettings final : public QObject {
    Q_OBJECT

public:
    explicit ApplicationSettings(QObject* parent = nullptr);

    [[nodiscard]] support::ReceiverConfig config() const;
    void setConfig(const support::ReceiverConfig& config);

signals:
    void configChanged(const padmirror::support::ReceiverConfig& config);

private:
    support::ReceiverConfig config_;
};

} // namespace padmirror::app

Q_DECLARE_METATYPE(padmirror::support::ReceiverConfig)
