#include "app/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>

#if defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#endif
#include <cstdlib>
#include <vector>

namespace padmirror::app {
namespace {

struct AdapterEntry {
    QString name;
    QString ip;
};

std::vector<AdapterEntry> queryAvailableAdapters() {
    std::vector<AdapterEntry> result;
#if defined(_WIN32)
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES* adapters = nullptr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        adapters = static_cast<IP_ADAPTER_ADDRESSES*>(std::malloc(size));
        if (!adapters) {
            return result;
        }
        ULONG status = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST,
            nullptr, adapters, &size);
        if (status != ERROR_BUFFER_OVERFLOW) {
            break;
        }
        std::free(adapters);
        adapters = nullptr;
    }
    if (!adapters) {
        return result;
    }

    for (auto* it = adapters; it; it = it->Next) {
        if (it->OperStatus != IfOperStatusUp ||
            it->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            !it->Ipv4Enabled) {
            continue;
        }
        for (auto* unicast = it->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr ||
                unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
            char ipBuf[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &sin->sin_addr, ipBuf, sizeof(ipBuf))) {
                QString ipStr = QString::fromLatin1(ipBuf);
                if (ipStr.startsWith(QStringLiteral("127.")) ||
                    ipStr.startsWith(QStringLiteral("169.254."))) {
                    continue;
                }
                QString friendly = it->FriendlyName
                    ? QString::fromWCharArray(it->FriendlyName)
                    : QString::fromLatin1(it->AdapterName);
                result.push_back({friendly, ipStr});
            }
        }
    }
    std::free(adapters);
#endif
    return result;
}

} // namespace

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

    networkInterface_ = new QComboBox(this);
    networkInterface_->addItem(QStringLiteral("Auto (Khuyên dùng / Recommended)"), QString());
    const auto adapters = queryAvailableAdapters();
    for (const auto& adapter : adapters) {
        const QString label = QStringLiteral("%1 (%2)").arg(adapter.name, adapter.ip);
        networkInterface_->addItem(label, adapter.ip);
    }
    const QString currentBind = QString::fromStdString(config.bindAddress);
    int selectedIdx = networkInterface_->findData(currentBind);
    if (selectedIdx >= 0) {
        networkInterface_->setCurrentIndex(selectedIdx);
    } else {
        networkInterface_->setCurrentIndex(0);
    }
    form->addRow(QStringLiteral("Network adapter"), networkInterface_);

    qualityProfile_ = new QComboBox(this);
    qualityProfile_->addItem(QStringLiteral("720p (1280×720)"),
                             static_cast<int>(support::QualityProfile::HD_720));
    qualityProfile_->addItem(QStringLiteral("1080p (1920×1080)"),
                             static_cast<int>(support::QualityProfile::FHD_1080));
    qualityProfile_->addItem(QStringLiteral("1440p (2560×1440)"),
                             static_cast<int>(support::QualityProfile::QHD_1440));
    qualityProfile_->setCurrentIndex(
        qualityProfile_->findData(static_cast<int>(config.qualityProfile)));
    form->addRow(QStringLiteral("Quality"), qualityProfile_);

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
    config.bindAddress = networkInterface_->currentData().toString().toUtf8().toStdString();
    config.qualityProfile = static_cast<support::QualityProfile>(
        qualityProfile_->currentData().toInt());
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
