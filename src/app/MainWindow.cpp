#include "app/MainWindow.h"

#include <QCloseEvent>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QShowEvent>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace padmirror::app {
namespace {

QString titleFor(airplay::ReceiverState state) {
    switch (state) {
    case airplay::ReceiverState::Starting:
        return QStringLiteral("STARTING");
    case airplay::ReceiverState::Waiting:
        return QStringLiteral("READY TO MIRROR");
    case airplay::ReceiverState::Streaming:
        return QStringLiteral("STREAMING");
    case airplay::ReceiverState::Error:
        return QStringLiteral("RECEIVER ERROR");
    }
    return {};
}

QString badgeFor(airplay::ReceiverState state) {
    switch (state) {
    case airplay::ReceiverState::Starting:
        return QStringLiteral("INITIALIZING");
    case airplay::ReceiverState::Waiting:
        return QStringLiteral("AIRPLAY");
    case airplay::ReceiverState::Streaming:
        return QStringLiteral("LIVE");
    case airplay::ReceiverState::Error:
        return QStringLiteral("ERROR");
    }
    return {};
}

QString colorFor(airplay::ReceiverState state) {
    return state == airplay::ReceiverState::Error
        ? QStringLiteral("#ff695e")
        : QStringLiteral("#b7f34a");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("PadMirror"));
    setWindowIcon(QIcon(QStringLiteral(":/assets/icons/PadMirror-256.png")));
    setMinimumSize(640, 400);
    resize(1280, 800);
    setStyleSheet(QStringLiteral("background: #050708;"));

    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);

    videoHost_ = new QWidget(this);
    videoHost_->setAttribute(Qt::WA_NativeWindow);
    videoHost_->setAttribute(Qt::WA_OpaquePaintEvent);
    videoHost_->setAutoFillBackground(true);
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
        QStringLiteral("On iPad, open Control Center, choose Screen Mirroring, then select PadMirror."),
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

    receiver_ = std::make_unique<airplay::UxPlayReceiver>(
        [this](airplay::ReceiverState state, const std::string& detail) {
            const QString text = QString::fromUtf8(detail);
            QMetaObject::invokeMethod(
                this,
                [this, state, text] { applyReceiverState(state, text); },
                Qt::QueuedConnection);
        });

    applyReceiverState(airplay::ReceiverState::Starting,
                       QStringLiteral("Preparing the in-process UxPlay receiver"));
}

MainWindow::~MainWindow() {
    if (receiver_) {
        receiver_->stop();
    }
}

void MainWindow::startReceiver() {
    if (receiverStartRequested_) {
        return;
    }
    receiverStartRequested_ = true;

    const auto handle = static_cast<std::uintptr_t>(videoHost_->winId());
    const auto result = receiver_->start("PadMirror", handle);
    if (!result.started) {
        applyReceiverState(airplay::ReceiverState::Error,
                           QString::fromUtf8(result.detail));
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (receiver_) {
        receiver_->setWindowHandle(static_cast<std::uintptr_t>(videoHost_->winId()));
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (receiver_) {
        receiver_->stop();
    }
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
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
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

    statePanel_->setVisible(state != airplay::ReceiverState::Streaming);
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

} // namespace padmirror::app
