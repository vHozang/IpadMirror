#pragma once

#include <QWidget>

#include <cstdint>

class QQuickWidget;
class QLabel;
class QStackedLayout;

namespace padmirror::app {

class AppController;

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);
    ~MainWindow() override = default;

    void toggleFullscreen();
    void beginSystemMove();
    void setAlwaysOnTop(bool enabled);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateLayerVisibility();
    void updateStreamStatus();

    AppController* controller_ = nullptr;
    QStackedLayout* pages_ = nullptr;
    QWidget* videoPage_ = nullptr;
    QWidget* videoHost_ = nullptr;
    QWidget* streamBar_ = nullptr;
    QLabel* streamStatus_ = nullptr;
    QQuickWidget* overlay_ = nullptr;
};

} // namespace padmirror::app
