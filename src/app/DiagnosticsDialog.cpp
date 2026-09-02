#include "app/DiagnosticsDialog.h"

#include "app/Logging.h"

#include <gst/gst.h>

#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <string>

namespace padmirror::app {
namespace {

QString stateName(airplay::ReceiverState state) {
    switch (state) {
    case airplay::ReceiverState::Starting: return QStringLiteral("Starting");
    case airplay::ReceiverState::Waiting: return QStringLiteral("Ready");
    case airplay::ReceiverState::Connecting: return QStringLiteral("Connecting");
    case airplay::ReceiverState::Streaming: return QStringLiteral("Streaming");
    case airplay::ReceiverState::Stopping: return QStringLiteral("Stopped");
    case airplay::ReceiverState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString textOrDash(const std::string& value) {
    return value.empty()
        ? QStringLiteral("—")
        : QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

DiagnosticsDialog::DiagnosticsDialog(AirPlayController* controller, QWidget* parent)
    : QDialog(parent),
      controller_(controller) {
    setWindowTitle(QStringLiteral("PadMirror Diagnostics"));
    resize(560, 430);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    addValue(form, QStringLiteral("PadMirror"))
        ->setText(QCoreApplication::applicationVersion());
    addValue(form, QStringLiteral("UxPlay backend"))
        ->setText(QStringLiteral("in-process native receiver"));
    addValue(form, QStringLiteral("GStreamer"))
        ->setText(QString::fromLatin1(gst_version_string()));
    state_ = addValue(form, QStringLiteral("State"));
    receiver_ = addValue(form, QStringLiteral("Receiver"));
    port_ = addValue(form, QStringLiteral("RAOP listener"));
    connections_ = addValue(form, QStringLiteral("Connections"));
    client_ = addValue(form, QStringLiteral("Client"));
    video_ = addValue(form, QStringLiteral("Video"));
    audio_ = addValue(form, QStringLiteral("Audio"));
    addValue(form, QStringLiteral("Renderer"))
        ->setText(QStringLiteral("GStreamer D3D11 / WASAPI2"));
    addValue(form, QStringLiteral("Log directory"))
        ->setText(QDir::toNativeSeparators(logDirectoryPath()));
    layout->addLayout(form);
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* openLogs = buttons->addButton(QStringLiteral("Open Log Folder"),
                                        QDialogButtonBox::ActionRole);
    connect(openLogs, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectoryPath()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(controller_, &AirPlayController::diagnosticsChanged,
            this, &DiagnosticsDialog::refresh);
    connect(controller_, &AirPlayController::stateChanged,
            this, [this](airplay::ReceiverState, const QString&) {
                refresh(controller_->diagnostics());
            });
    refresh(controller_->diagnostics());
}

void DiagnosticsDialog::refresh(const airplay::ReceiverDiagnostics& diagnostics) {
    state_->setText(QStringLiteral("%1 — %2")
                        .arg(stateName(controller_->state()), controller_->stateDetail()));
    receiver_->setText(QStringLiteral("%1, max %2 FPS")
                           .arg(textOrDash(diagnostics.receiverName))
                           .arg(diagnostics.maxFps));
    port_->setText(diagnostics.port
        ? QStringLiteral("TCP %1").arg(diagnostics.port)
        : QStringLiteral("—"));
    connections_->setText(QString::number(diagnostics.connectionCount));
    const auto clientName = textOrDash(diagnostics.clientName);
    const auto clientModel = textOrDash(diagnostics.clientModel);
    client_->setText(QStringLiteral("%1 (%2)").arg(clientName, clientModel));
    video_->setText(diagnostics.videoWidth > 0
        ? QStringLiteral(
              "%1, %2 × %3\n"
              "input %4 FPS, sink %5 FPS, queue %6/3\n"
              "PTS input/sink %7/%8 ms, lead %9 ms\n"
              "queue overruns %10, QoS drops %11, appsrc failures %12")
              .arg(textOrDash(diagnostics.videoCodec))
              .arg(diagnostics.videoWidth)
              .arg(diagnostics.videoHeight)
              .arg(diagnostics.videoInputFps, 0, 'f', 1)
              .arg(diagnostics.videoSinkFps, 0, 'f', 1)
              .arg(diagnostics.videoQueueBuffers)
              .arg(diagnostics.videoInputPtsDeltaMs, 0, 'f', 1)
              .arg(diagnostics.videoSinkPtsDeltaMs, 0, 'f', 1)
              .arg(diagnostics.videoSinkPtsLeadMs, 0, 'f', 1)
              .arg(diagnostics.videoQueueDropped)
              .arg(diagnostics.videoQosDropped)
              .arg(diagnostics.videoAppsrcFailures)
        : textOrDash(diagnostics.videoCodec));
    audio_->setText(diagnostics.audioSampleRate > 0
        ? QStringLiteral(
              "%1, %2 Hz\n"
              "appsrc %3 ms, queue %4 ms, PTS lead %5 ms\n"
              "drops %6/%7, failures %8, recoveries %9\n"
              "reanchors %10, concealed %11, decoder resets %12")
              .arg(textOrDash(diagnostics.audioCodec))
              .arg(diagnostics.audioSampleRate)
              .arg(static_cast<double>(diagnostics.audioAppsrcQueuedNs) / 1'000'000.0, 0, 'f', 1)
              .arg(static_cast<double>(diagnostics.audioQueueQueuedNs) / 1'000'000.0, 0, 'f', 1)
              .arg(static_cast<double>(diagnostics.audioPtsLeadNs) / 1'000'000.0, 0, 'f', 1)
              .arg(diagnostics.audioAppsrcDropped)
              .arg(diagnostics.audioQueueDropped)
              .arg(diagnostics.audioAppsrcFailures)
              .arg(diagnostics.audioLatencyRecoveries)
              .arg(diagnostics.audioReanchors)
              .arg(diagnostics.audioConcealedFrames)
              .arg(diagnostics.audioDecoderResets)
        : textOrDash(diagnostics.audioCodec));
}

QLabel* DiagnosticsDialog::addValue(QFormLayout* form, const QString& label) {
    auto* value = new QLabel(QStringLiteral("—"), this);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setWordWrap(true);
    form->addRow(label, value);
    return value;
}

} // namespace padmirror::app
