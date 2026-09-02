#pragma once

#include "app/AirPlayController.h"

#include <QDialog>

class QFormLayout;
class QLabel;

namespace padmirror::app {

class DiagnosticsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DiagnosticsDialog(AirPlayController* controller,
                               QWidget* parent = nullptr);

private:
    void refresh(const airplay::ReceiverDiagnostics& diagnostics);
    QLabel* addValue(QFormLayout* form, const QString& label);

    AirPlayController* controller_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* receiver_ = nullptr;
    QLabel* port_ = nullptr;
    QLabel* connections_ = nullptr;
    QLabel* client_ = nullptr;
    QLabel* video_ = nullptr;
    QLabel* audio_ = nullptr;
};

} // namespace padmirror::app
