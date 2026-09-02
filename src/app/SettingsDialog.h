#pragma once

#include "app/ApplicationSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSlider;
class QLabel;

namespace padmirror::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationSettings* settings,
                            QWidget* parent = nullptr);

private:
    void accept() override;

    ApplicationSettings* settings_ = nullptr;
    QLineEdit* receiverName_ = nullptr;
    QComboBox* maxFps_ = nullptr;
    QSlider* volume_ = nullptr;
    QLabel* volumeValue_ = nullptr;
    QCheckBox* muted_ = nullptr;
    QCheckBox* keepAwake_ = nullptr;
    QCheckBox* startOnLaunch_ = nullptr;
    QCheckBox* fullscreenOnConnect_ = nullptr;
};

} // namespace padmirror::app
