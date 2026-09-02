#include "app/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>

namespace padmirror::app {

SettingsDialog::SettingsDialog(ApplicationSettings* settings, QWidget* parent)
    : QDialog(parent),
      settings_(settings) {
    setWindowTitle(QStringLiteral("PadMirror Settings"));
    setModal(true);
    resize(460, 360);

    const auto config = settings_->config();
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    receiverName_ = new QLineEdit(
        QString::fromUtf8(config.receiverName.data(),
                          static_cast<qsizetype>(config.receiverName.size())),
        this);
    receiverName_->setMaxLength(63);
    form->addRow(QStringLiteral("Receiver name"), receiverName_);

    maxFps_ = new QComboBox(this);
    maxFps_->addItem(QStringLiteral("60 FPS"), 60);
    maxFps_->setEnabled(false);
    maxFps_->setToolTip(QStringLiteral(
        "PadMirror always advertises the 60 FPS AirPlay path."));
    form->addRow(QStringLiteral("Frame rate"), maxFps_);

    auto* volumeRow = new QWidget(this);
    auto* volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(0, 0, 0, 0);
    volume_ = new QSlider(Qt::Horizontal, volumeRow);
    volume_->setRange(0, 100);
    volume_->setValue(config.volumePercent);
    volumeValue_ = new QLabel(QStringLiteral("%1%").arg(config.volumePercent), volumeRow);
    volumeValue_->setMinimumWidth(42);
    volumeLayout->addWidget(volume_);
    volumeLayout->addWidget(volumeValue_);
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        volumeValue_->setText(QStringLiteral("%1%").arg(value));
    });
    form->addRow(QStringLiteral("Output volume"), volumeRow);

    muted_ = new QCheckBox(QStringLiteral("Mute receiver audio"), this);
    muted_->setChecked(config.muted);
    form->addRow(QString(), muted_);

    keepAwake_ = new QCheckBox(QStringLiteral("Keep display awake while streaming"), this);
    keepAwake_->setChecked(config.keepAwakeWhileStreaming);
    form->addRow(QString(), keepAwake_);

    startOnLaunch_ = new QCheckBox(QStringLiteral("Start receiver when PadMirror opens"), this);
    startOnLaunch_->setChecked(config.startReceiverOnLaunch);
    form->addRow(QString(), startOnLaunch_);

    fullscreenOnConnect_ = new QCheckBox(QStringLiteral("Enter fullscreen when mirroring starts"), this);
    fullscreenOnConnect_->setChecked(config.fullscreenOnConnect);
    form->addRow(QString(), fullscreenOnConnect_);

    layout->addLayout(form);
    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::accept() {
    auto config = settings_->config();
    config.receiverName = receiverName_->text().toUtf8().toStdString();
    config.maxFps = maxFps_->currentData().toInt();
    config.volumePercent = volume_->value();
    config.muted = muted_->isChecked();
    config.keepAwakeWhileStreaming = keepAwake_->isChecked();
    config.startReceiverOnLaunch = startOnLaunch_->isChecked();
    config.fullscreenOnConnect = fullscreenOnConnect_->isChecked();
    settings_->setConfig(config);
    QDialog::accept();
}

} // namespace padmirror::app
