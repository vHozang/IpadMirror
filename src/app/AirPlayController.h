#pragma once

#include "airplay/UxPlayReceiver.h"
#include "app/ApplicationSettings.h"

#include <QObject>
#include <QMetaType>
#include <QString>

#include <cstdint>
#include <memory>

namespace padmirror::app {

class AirPlayController final : public QObject {
    Q_OBJECT

public:
    explicit AirPlayController(ApplicationSettings* settings,
                               QObject* parent = nullptr);
    ~AirPlayController() override;

    void setVideoWindowHandle(std::uintptr_t windowHandle);
    [[nodiscard]] airplay::ReceiverState state() const;
    [[nodiscard]] QString stateDetail() const;
    [[nodiscard]] airplay::ReceiverDiagnostics diagnostics() const;
    [[nodiscard]] bool running() const;

public slots:
    void start();
    void stop();
    void restart();

signals:
    void stateChanged(padmirror::airplay::ReceiverState state,
                      const QString& detail);
    void diagnosticsChanged(const padmirror::airplay::ReceiverDiagnostics& diagnostics);
    void receiverRestarted();

private slots:
    void applyConfig(const padmirror::support::ReceiverConfig& config);

private:
    void applyState(airplay::ReceiverState state, const QString& detail);
    void applyDiagnostics(const airplay::ReceiverDiagnostics& diagnostics);
    void updatePowerRequest();

    ApplicationSettings* settings_ = nullptr;
    std::unique_ptr<airplay::UxPlayReceiver> receiver_;
    support::ReceiverConfig config_;
    airplay::ReceiverDiagnostics diagnostics_;
    airplay::ReceiverState state_ = airplay::ReceiverState::Starting;
    QString detail_;
    std::uintptr_t windowHandle_ = 0;
};

} // namespace padmirror::app

Q_DECLARE_METATYPE(padmirror::airplay::ReceiverState)
Q_DECLARE_METATYPE(padmirror::airplay::ReceiverDiagnostics)
