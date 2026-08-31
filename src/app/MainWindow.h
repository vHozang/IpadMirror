#pragma once

#include "airplay/UxPlayReceiver.h"

#include <QWidget>

#include <memory>

class QLabel;
class QStackedWidget;
class QKeyEvent;
class QCloseEvent;
class QShowEvent;

namespace padmirror::app {

class MainWindow final : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void startReceiver();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyReceiverState(airplay::ReceiverState state, const QString& detail);
    void toggleFullscreen();

    QWidget* videoHost_ = nullptr;
    QWidget* statePanel_ = nullptr;
    QLabel* stateBadge_ = nullptr;
    QLabel* stateTitle_ = nullptr;
    QLabel* stateDetail_ = nullptr;
    std::unique_ptr<airplay::UxPlayReceiver> receiver_;
    bool receiverStartRequested_ = false;
};

} // namespace padmirror::app
