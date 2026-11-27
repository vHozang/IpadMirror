#pragma once

#include "app/AirPlayController.h"
#include "app/ApplicationSettings.h"

#include <QWidget>

class QAction;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QMenu;
class QShowEvent;
class QSystemTrayIcon;
class QTimer;
class QToolBar;

namespace padmirror::app {

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    MainWindow(ApplicationSettings* settings,
               AirPlayController* controller,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    void startReceiver();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyReceiverState(airplay::ReceiverState state, const QString& detail);
    void showSettings();
    void showDiagnostics();
    void setupSystemTray();
    void quitApplication();
    void toggleFullscreen();
    void setControlsVisible(bool visible);

    ApplicationSettings* settings_ = nullptr;
    AirPlayController* controller_ = nullptr;
    QWidget* videoHost_ = nullptr;
    QWidget* statePanel_ = nullptr;
    QLabel* stateBadge_ = nullptr;
    QLabel* stateTitle_ = nullptr;
    QLabel* stateDetail_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QTimer* controlsHideTimer_ = nullptr;
    QAction* receiverAction_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayMenu_ = nullptr;
};

} // namespace padmirror::app
