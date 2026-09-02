#include "app/MainWindow.h"

#include "app/DiagnosticsDialog.h"
#include "app/SettingsDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QStackedLayout>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace padmirror::app {
namespace {

QString titleFor(airplay::ReceiverState state) {
    switch (state) {
    case airplay::ReceiverState::Starting: return QStringLiteral("STARTING");
    case airplay::ReceiverState::Waiting: return QStringLiteral("READY TO MIRROR");
    case airplay::ReceiverState::Connecting: return QStringLiteral("CONNECTING");
    case airplay::ReceiverState::Streaming: return QStringLiteral("STREAMING");
    case airplay::ReceiverState::Stopping: return QStringLiteral("RECEIVER STOPPED");
    case airplay::ReceiverState::Error: return QStringLiteral("RECEIVER ERROR");
    }
    return {};
}

QString badgeFor(airplay::ReceiverState state) {
    switch (state) {
    case airplay::ReceiverState::Starting: return QStringLiteral("INITIALIZING");
    case airplay::ReceiverState::Waiting: return QStringLiteral("AIRPLAY");
    case airplay::ReceiverState::Connecting: return QStringLiteral("CONNECTING");
    case airplay::ReceiverState::Streaming: return QStringLiteral("LIVE");
    case airplay::ReceiverState::Stopping: return QStringLiteral("OFFLINE");
    case airplay::ReceiverState::Error: return QStringLiteral("ERROR");
    }
    return {};
}

QString colorFor(airplay::ReceiverState state) {
    return state == airplay::ReceiverState::Error
        ? QStringLiteral("#ff695e")
        : state == airplay::ReceiverState::Stopping
            ? QStringLiteral("#8c969a")
            : QStringLiteral("#b7f34a");
}

} // namespace

MainWindow::MainWindow(ApplicationSettings* settings,
                       AirPlayController* controller,
                       QWidget* parent)
    : QWidget(parent),
      settings_(settings),
      controller_(controller) {
    setWindowTitle(QStringLiteral("PadMirror"));
    setWindowIcon(QIcon(QStringLiteral(":/assets/icons/PadMirror-256.png")));
    setMinimumSize(640, 400);
    resize(1280, 800);
    setMouseTracking(true);
    setStyleSheet(QStringLiteral("background: #050708;"));

    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);

    videoHost_ = new QWidget(this);
    videoHost_->setAttribute(Qt::WA_NativeWindow);
    videoHost_->setAttribute(Qt::WA_OpaquePaintEvent);
    videoHost_->setAutoFillBackground(true);
    videoHost_->setMouseTracking(true);
    videoHost_->installEventFilter(this);
    auto palette = videoHost_->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#000000")));
    videoHost_->setPalette(palette);
    stack->addWidget(videoHost_);

    statePanel_ = new QWidget(this);
    statePanel_->setAttribute(Qt::WA_StyledBackground);
    statePanel_->setStyleSheet(QStringLiteral("background: #07090a;"));
    auto* panelLayout = new QVBoxLayout(statePanel_);
    panelLayout->setContentsMargins(48, 48, 48, 48);
    panelLayout->addStretch(1);

    auto* card = new QFrame(statePanel_);
    card->setMaximumWidth(620);
    card->setObjectName(QStringLiteral("receiverCard"));
    card->setStyleSheet(QStringLiteral(
        "QFrame#receiverCard { background: #0d1113; border: 1px solid #283136; "
        "border-radius: 14px; }"
        "QLabel { background: transparent; color: #edf2eb; font-family: 'Segoe UI'; }"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(40, 36, 40, 40);
    cardLayout->setSpacing(14);

    auto* brand = new QLabel(QStringLiteral("PADMIRROR"), card);
    auto brandFont = brand->font();
    brandFont.setPointSize(10);
    brandFont.setBold(true);
    brandFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    brand->setFont(brandFont);
    brand->setStyleSheet(QStringLiteral("color: #7f8a8e;"));
    cardLayout->addWidget(brand);

    stateBadge_ = new QLabel(card);
    stateBadge_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    cardLayout->addWidget(stateBadge_);

    stateTitle_ = new QLabel(card);
    auto titleFont = stateTitle_->font();
    titleFont.setPointSize(25);
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    stateTitle_->setFont(titleFont);
    cardLayout->addWidget(stateTitle_);

    stateDetail_ = new QLabel(card);
    stateDetail_->setWordWrap(true);
    stateDetail_->setStyleSheet(QStringLiteral("color: #a3adb1; font-size: 14px;"));
    cardLayout->addWidget(stateDetail_);

    auto* instructions = new QLabel(
        QStringLiteral("On iPad, open Control Center, choose Screen Mirroring, then select the PadMirror receiver."),
        card);
    instructions->setWordWrap(true);
    instructions->setStyleSheet(QStringLiteral(
        "color: #7f8a8e; font-size: 12px; border-top: 1px solid #283136; "
        "padding-top: 18px; margin-top: 8px;"));
    cardLayout->addWidget(instructions);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(card);
    row->addStretch(1);
    panelLayout->addLayout(row);
    panelLayout->addStretch(1);
    stack->addWidget(statePanel_);

    toolbar_ = new QToolBar(this);
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(18, 18));
    toolbar_->setStyleSheet(QStringLiteral(
        "QToolBar { background: rgba(10, 14, 16, 220); border: 0; padding: 6px; }"
        "QToolButton { color: #edf2eb; padding: 6px 10px; }"));
    receiverAction_ = toolbar_->addAction(QStringLiteral("Stop Receiver"));
    connect(receiverAction_, &QAction::triggered, this, [this] {
        if (controller_->running()) {
            controller_->stop();
        } else {
            controller_->start();
        }
    });
    toolbar_->addSeparator();
    auto* settingsAction = toolbar_->addAction(QStringLiteral("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    auto* diagnosticsAction = toolbar_->addAction(QStringLiteral("Diagnostics"));
    connect(diagnosticsAction, &QAction::triggered, this, &MainWindow::showDiagnostics);
    auto* fullscreenAction = toolbar_->addAction(QStringLiteral("Fullscreen  F11"));
    connect(fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    stack->addWidget(toolbar_);
    stack->setAlignment(toolbar_, Qt::AlignTop);

    controlsHideTimer_ = new QTimer(this);
    controlsHideTimer_->setSingleShot(true);
    controlsHideTimer_->setInterval(2500);
    connect(controlsHideTimer_, &QTimer::timeout, this, [this] {
        if (controller_->state() == airplay::ReceiverState::Streaming) {
            setControlsVisible(false);
        }
    });

    connect(controller_, &AirPlayController::stateChanged,
            this, &MainWindow::applyReceiverState);
    applyReceiverState(airplay::ReceiverState::Starting,
                       QStringLiteral("Preparing the in-process UxPlay receiver"));
}

MainWindow::~MainWindow() = default;

void MainWindow::startReceiver() {
    controller_->setVideoWindowHandle(
        static_cast<std::uintptr_t>(videoHost_->winId()));
    if (settings_->config().startReceiverOnLaunch) {
        controller_->start();
    } else {
        applyReceiverState(airplay::ReceiverState::Stopping,
                           QStringLiteral("Start the receiver from the toolbar when ready"));
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    controller_->setVideoWindowHandle(
        static_cast<std::uintptr_t>(videoHost_->winId()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    controller_->stop();
    QWidget::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == videoHost_) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            toggleFullscreen();
            return true;
        }
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) {
            setControlsVisible(true);
            if (controller_->state() == airplay::ReceiverState::Streaming) {
                controlsHideTimer_->start();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F11) {
        toggleFullscreen();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    toggleFullscreen();
    event->accept();
}

void MainWindow::applyReceiverState(airplay::ReceiverState state,
                                    const QString& detail) {
    stateTitle_->setText(titleFor(state));
    stateDetail_->setText(detail);

    const auto color = colorFor(state);
    stateBadge_->setText(badgeFor(state));
    stateBadge_->setStyleSheet(QStringLiteral(
        "QLabel { color: #07090a; background: %1; border-radius: 4px; "
        "padding: 5px 9px; font-size: 10px; font-weight: 700; }").arg(color));

    const bool streaming = state == airplay::ReceiverState::Streaming;
    videoHost_->setVisible(streaming);
    statePanel_->setVisible(!streaming);
    if (streaming) {
        setControlsVisible(true);
        controlsHideTimer_->start();
    } else {
        controlsHideTimer_->stop();
        setControlsVisible(true);
    }
    receiverAction_->setText(controller_->running()
        ? QStringLiteral("Stop Receiver")
        : QStringLiteral("Start Receiver"));
    if (state == airplay::ReceiverState::Streaming &&
        settings_->config().fullscreenOnConnect && !isFullScreen()) {
        showFullScreen();
    }
}

void MainWindow::showSettings() {
    SettingsDialog dialog(settings_, this);
    dialog.exec();
}

void MainWindow::showDiagnostics() {
    DiagnosticsDialog dialog(controller_, this);
    dialog.exec();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::setControlsVisible(bool visible) {
    toolbar_->setVisible(visible);
}

} // namespace padmirror::app
