#pragma once

#include <QWidget>

namespace padmirror::app {

class AppController;
class Settings;

class SettingsWindow final : public QWidget {
    Q_OBJECT

public:
    SettingsWindow(AppController* controller, Settings* settings, QWidget* parent = nullptr);

    void showSettings();
};

} // namespace padmirror::app
