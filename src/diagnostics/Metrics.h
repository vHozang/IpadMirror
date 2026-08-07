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
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY changed)
    Q_PROPERTY(double audioBufferMs READ audioBufferMs NOTIFY changed)
    Q_PROPERTY(qulonglong audioUnderruns READ audioUnderruns NOTIFY changed)
    Q_PROPERTY(qulonglong audioResyncs READ audioResyncs NOTIFY changed)
    Q_PROPERTY(QString transport READ transport NOTIFY changed)
    Q_PROPERTY(QString audioBackend READ audioBackend NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)

public:
    explicit Metrics(QObject* parent = nullptr);

    void sourceFrame();
    void decodedFrame();
    void renderedFrame();
    void droppedFrame(std::uint64_t count = 1);
    void audioUnderrun();
    void audioResync();
    void setAudioBufferMs(double value);
    void setTransport(const QString& value);
    void setAudioBackend(const QString& value);
    void setDeviceName(const QString& value);
    void reset();

    [[nodiscard]] double sourceFps() const;
    [[nodiscard]] double decodeFps() const;
    [[nodiscard]] double renderFps() const;
    [[nodiscard]] qulonglong droppedFrames() const;
    [[nodiscard]] double audioBufferMs() const;
    [[nodiscard]] qulonglong audioUnderruns() const;
    [[nodiscard]] qulonglong audioResyncs() const;
    [[nodiscard]] QString transport() const;
    [[nodiscard]] QString audioBackend() const;
    [[nodiscard]] QString deviceName() const;

signals:
    void changed();

private:
    FpsCounter sourceCounter_;
    FpsCounter decodeCounter_;
    FpsCounter renderCounter_;
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::atomic<std::uint64_t> audioUnderruns_{0};
    std::atomic<std::uint64_t> audioResyncs_{0};
    std::atomic<double> audioBufferMs_{0.0};
    mutable std::mutex textMutex_;
    QString transport_ = QStringLiteral("USB");
    QString audioBackend_ = QStringLiteral("Not started");
    QString deviceName_ = QStringLiteral("iPad");
    QTimer refreshTimer_;
};

} // namespace padmirror::diagnostics
