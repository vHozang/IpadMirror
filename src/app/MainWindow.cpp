#include "app/MainWindow.h"

#include "app/AppController.h"

#include <QCloseEvent>
#include <QDebug>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickWidget>
#include <QShowEvent>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWindow>

namespace padmirror::app {

MainWindow::MainWindow(AppController* controller, QWidget* parent)
    : QWidget(parent),
      controller_(controller) {
    setWindowTitle(QStringLiteral("PadMirror"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow);
    setAutoFillBackground(true);
    auto windowPalette = palette();
    windowPalette.setColor(QPalette::Window, QColor(QStringLiteral("#07090a")));
    setPalette(windowPalette);
    setMinimumSize(960, 600);
    resize(1280, 800);

    pages_ = new QStackedLayout(this);
    pages_->setContentsMargins(0, 0, 0, 0);
    pages_->setStackingMode(QStackedLayout::StackOne);

    videoPage_ = new QWidget(this);
    videoPage_->setObjectName(QStringLiteral("videoPage"));
    videoPage_->setStyleSheet(QStringLiteral("background: #050708;"));
    auto* videoLayout = new QVBoxLayout(videoPage_);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    streamBar_ = new QWidget(videoPage_);
    streamBar_->setObjectName(QStringLiteral("streamBar"));
    streamBar_->setFixedHeight(48);
    streamBar_->installEventFilter(this);
    streamBar_->setStyleSheet(QStringLiteral(
        "QWidget#streamBar { background: #0c0f10; border-bottom: 1px solid #273034; }"
        "QLabel#streamTitle { color: #eef3eb; font-family: Bahnschrift; font-size: 15px; "
        "font-weight: 700; letter-spacing: 2px; }"
        "QLabel#streamStatus { color: #899399; font-family: Bahnschrift; font-size: 11px; }"
        "QPushButton { color: #eef3eb; background: transparent; border: 1px solid #273034; "
        "border-radius: 4px; padding: 6px 12px; font-family: Bahnschrift; font-size: 10px; "
        "font-weight: 700; }"
        "QPushButton:hover { background: #22292c; }"
        "QPushButton#stopButton { color: #07090a; background: #b7f34a; border-color: #b7f34a; }"
        "QPushButton#closeButton:hover { background: #c9473d; border-color: #c9473d; }"));
    auto* barLayout = new QHBoxLayout(streamBar_);
    barLayout->setContentsMargins(16, 7, 10, 7);
    barLayout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("PADMIRROR"), streamBar_);
    title->setObjectName(QStringLiteral("streamTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(title);

    streamStatus_ = new QLabel(streamBar_);
    streamStatus_->setObjectName(QStringLiteral("streamStatus"));
    streamStatus_->setAttribute(Qt::WA_TransparentForMouseEvents);
    barLayout->addWidget(streamStatus_, 1);

    const auto addButton = [this, barLayout](
                               const QString& label,
                               const QString& objectName,
                               const auto& handler) {
        auto* button = new QPushButton(label, streamBar_);
        button->setObjectName(objectName);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, handler);
        barLayout->addWidget(button);
    };
    addButton(QStringLiteral("STOP"), QStringLiteral("stopButton"), [this] { controller_->stop(); });
    addButton(QStringLiteral("FULLSCREEN"), QString(), [this] { toggleFullscreen(); });
    addButton(QStringLiteral("_"), QString(), [this] { showMinimized(); });
    addButton(QStringLiteral("[]"), QString(), [this] { controller_->toggleMaximize(); });
    addButton(QStringLiteral("X"), QStringLiteral("closeButton"), [this] { close(); });
    videoLayout->addWidget(streamBar_);

    videoHost_ = new QWidget(videoPage_);
    videoHost_->setObjectName(QStringLiteral("videoHost"));
    videoHost_->setAttribute(Qt::WA_NativeWindow);
    videoHost_->setStyleSheet(QStringLiteral("background: #050708;"));
    videoLayout->addWidget(videoHost_, 1);
    pages_->addWidget(videoPage_);

    overlay_ = new QQuickWidget(this);
    overlay_->setObjectName(QStringLiteral("overlay"));
    overlay_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    overlay_->setClearColor(QColor(QStringLiteral("#07090a")));
    overlay_->rootContext()->setContextProperty(QStringLiteral("app"), controller_);
    overlay_->rootContext()->setContextProperty(QStringLiteral("settings"), controller_->settings());
    overlay_->rootContext()->setContextProperty(QStringLiteral("devices"), controller_->devices());
    overlay_->rootContext()->setContextProperty(QStringLiteral("metrics"), controller_->metrics());
    connect(overlay_, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status != QQuickWidget::Error) return;
        for (const auto& error : overlay_->errors()) {
            qCritical().noquote() << "QML:" << error.toString();
        }
    });
    overlay_->setSource(QUrl(QStringLiteral("qrc:/ui/Main.qml")));
    pages_->addWidget(overlay_);
    pages_->setCurrentWidget(overlay_);

    controller_->setMainWindow(this);
    connect(controller_, &AppController::stateChanged,
            this, &MainWindow::updateLayerVisibility);
    connect(controller_->metrics(), &diagnostics::Metrics::changed,
            this, &MainWindow::updateStreamStatus);
    updateLayerVisibility();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
    updateLayerVisibility();
}

void MainWindow::beginSystemMove() {
    if (windowHandle()) windowHandle()->startSystemMove();
}

void MainWindow::setAlwaysOnTop(bool enabled) {
    const bool visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (visible) show();
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    controller_->setVideoWindowHandle(static_cast<std::uintptr_t>(videoHost_->winId()));
    updateLayerVisibility();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    controller_->stop();
    QWidget::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        updateLayerVisibility();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == streamBar_) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                beginSystemMove();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                controller_->toggleMaximize();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::updateLayerVisibility() {
    const bool streaming = controller_->streaming();
    pages_->setCurrentWidget(streaming ? videoPage_ : overlay_);
    streamBar_->setVisible(streaming && !isFullScreen());
    updateStreamStatus();
}

void MainWindow::updateStreamStatus() {
    if (!streamStatus_) return;
    const auto fps = qRound(controller_->metrics()->renderFps());
    streamStatus_->setText(QStringLiteral("%1  /  %2  /  %3 FPS")
        .arg(controller_->connectionLabel(), controller_->statusText())
        .arg(fps));
}

} // namespace padmirror::app
