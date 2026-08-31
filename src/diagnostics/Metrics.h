#pragma once

#include "diagnostics/FpsCounter.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace padmirror::diagnostics {

class Metrics final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double sourceFps READ sourceFps NOTIFY changed)
    Q_PROPERTY(double decodeFps READ decodeFps NOTIFY changed)
    Q_PROPERTY(double renderFps READ renderFps NOTIFY changed)
    Q_PROPERTY(qulonglong sourceFrames READ sourceFrames NOTIFY changed)
    Q_PROPERTY(qulonglong decodedFrames READ decodedFrames NOTIFY changed)
    Q_PROPERTY(qulonglong renderedFrames READ renderedFrames NOTIFY changed)
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY changed)
    Q_PROPERTY(double audioBufferMs READ audioBufferMs NOTIFY changed)
    Q_PROPERTY(bool audioBufferValid READ audioBufferValid NOTIFY changed)
    Q_PROPERTY(qulonglong audioPackets READ audioPackets NOTIFY changed)
    Q_PROPERTY(qulonglong audioUnderruns READ audioUnderruns NOTIFY changed)
    Q_PROPERTY(qulonglong audioResyncs READ audioResyncs NOTIFY changed)
    Q_PROPERTY(double avSyncOffsetMs READ avSyncOffsetMs NOTIFY changed)
    Q_PROPERTY(bool avSyncValid READ avSyncValid NOTIFY changed)
    Q_PROPERTY(QString transport READ transport NOTIFY changed)
    Q_PROPERTY(QString transportState READ transportState NOTIFY changed)
    Q_PROPERTY(QString healthDetail READ healthDetail NOTIFY changed)
    Q_PROPERTY(QString audioBackend READ audioBackend NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY changed)
    Q_PROPERTY(QString sourceResolution READ sourceResolution NOTIFY changed)
    Q_PROPERTY(QString requestedQuality READ requestedQuality NOTIFY changed)
    Q_PROPERTY(qulonglong activeEpoch READ activeEpoch NOTIFY changed)
    Q_PROPERTY(QString lastDiscontinuity READ lastDiscontinuity NOTIFY changed)
    Q_PROPERTY(QString qualityStatus READ qualityStatus NOTIFY changed)

public:
    explicit Metrics(QObject* parent = nullptr);

    void sourceFrame();
    void decodedFrame();
    void renderedFrame();
    void videoTimestamp(std::uint64_t ptsNs);
    void audioPacket(std::uint64_t ptsNs);
    void droppedFrame(std::uint64_t count = 1);
    void audioUnderrun();
    void audioResync();
    void setAudioBufferMs(double value);
    void clearAudioBuffer();
    void setTransport(const QString& value);
    void setTransportState(const QString& value);
    void setHealthDetail(const QString& value);
    void setAudioBackend(const QString& value);
    void setDeviceName(const QString& value);
    void setVideoFormat(const QString& codec, int width, int height);
    void setRequestedQuality(const QString& value);
    void setActiveEpoch(std::uint64_t value);
    void setLastDiscontinuity(const QString& value);
    void setQualityStatus(const QString& value);
    void reset();

    [[nodiscard]] double sourceFps() const;
    [[nodiscard]] double decodeFps() const;
    [[nodiscard]] double renderFps() const;
    [[nodiscard]] qulonglong sourceFrames() const;
    [[nodiscard]] qulonglong decodedFrames() const;
    [[nodiscard]] qulonglong renderedFrames() const;
    [[nodiscard]] qulonglong droppedFrames() const;
    [[nodiscard]] double audioBufferMs() const;
    [[nodiscard]] bool audioBufferValid() const;
    [[nodiscard]] qulonglong audioPackets() const;
    [[nodiscard]] qulonglong audioUnderruns() const;
    [[nodiscard]] qulonglong audioResyncs() const;
    [[nodiscard]] double avSyncOffsetMs() const;
    [[nodiscard]] bool avSyncValid() const;
    [[nodiscard]] QString transport() const;
    [[nodiscard]] QString transportState() const;
    [[nodiscard]] QString healthDetail() const;
    [[nodiscard]] QString audioBackend() const;
    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString videoCodec() const;
    [[nodiscard]] QString sourceResolution() const;
    [[nodiscard]] QString requestedQuality() const;
    [[nodiscard]] qulonglong activeEpoch() const;
    [[nodiscard]] QString lastDiscontinuity() const;
    [[nodiscard]] QString qualityStatus() const;
    [[nodiscard]] int videoWidth() const;
    [[nodiscard]] int videoHeight() const;

signals:
    void changed();

private:
    FpsCounter sourceCounter_;
    FpsCounter decodeCounter_;
    FpsCounter renderCounter_;
    std::atomic<std::uint64_t> sourceFrames_{0};
    std::atomic<std::uint64_t> decodedFrames_{0};
    std::atomic<std::uint64_t> renderedFrames_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::atomic<std::uint64_t> audioPackets_{0};
    std::atomic<std::uint64_t> audioUnderruns_{0};
    std::atomic<std::uint64_t> audioResyncs_{0};
    std::atomic<std::uint64_t> latestVideoPtsNs_{0};
    std::atomic<std::uint64_t> latestAudioPtsNs_{0};
    std::atomic<double> audioBufferMs_{0.0};
    std::atomic<bool> audioBufferValid_{false};
    std::atomic<std::uint64_t> activeEpoch_{0};
    mutable std::mutex textMutex_;
    QString transport_ = QStringLiteral("--");
    QString transportState_ = QStringLiteral("Stopped");
    QString healthDetail_;
    QString audioBackend_ = QStringLiteral("Not started");
    QString deviceName_ = QStringLiteral("iPad");
    QString videoCodec_ = QStringLiteral("Waiting");
    QString requestedQuality_ = QStringLiteral("--");
    QString lastDiscontinuity_;
    QString qualityStatus_;
    std::atomic<int> videoWidth_{0};
    std::atomic<int> videoHeight_{0};
    QTimer refreshTimer_;
};

} // namespace padmirror::diagnostics
