#include "diagnostics/Metrics.h"

namespace padmirror::diagnostics {

Metrics::Metrics(QObject* parent)
    : QObject(parent) {
    refreshTimer_.setInterval(250);
    connect(&refreshTimer_, &QTimer::timeout, this, &Metrics::changed);
    refreshTimer_.start();
}

void Metrics::sourceFrame() { sourceCounter_.tick(); }
void Metrics::decodedFrame() { decodeCounter_.tick(); }
void Metrics::renderedFrame() { renderCounter_.tick(); }
void Metrics::droppedFrame(std::uint64_t count) { droppedFrames_.fetch_add(count); }
void Metrics::audioUnderrun() { audioUnderruns_.fetch_add(1); }
void Metrics::audioResync() { audioResyncs_.fetch_add(1); }
void Metrics::setAudioBufferMs(double value) { audioBufferMs_.store(value); }

void Metrics::setTransport(const QString& value) {
    std::lock_guard lock(textMutex_);
    transport_ = value;
}

void Metrics::setAudioBackend(const QString& value) {
    std::lock_guard lock(textMutex_);
    audioBackend_ = value;
}

void Metrics::setDeviceName(const QString& value) {
    std::lock_guard lock(textMutex_);
    deviceName_ = value;
}

void Metrics::reset() {
    sourceCounter_.reset();
    decodeCounter_.reset();
    renderCounter_.reset();
    droppedFrames_.store(0);
    audioUnderruns_.store(0);
    audioResyncs_.store(0);
    audioBufferMs_.store(0.0);
    emit changed();
}

double Metrics::sourceFps() const { return sourceCounter_.framesPerSecond(); }
double Metrics::decodeFps() const { return decodeCounter_.framesPerSecond(); }
double Metrics::renderFps() const { return renderCounter_.framesPerSecond(); }
qulonglong Metrics::droppedFrames() const { return droppedFrames_.load(); }
double Metrics::audioBufferMs() const { return audioBufferMs_.load(); }
qulonglong Metrics::audioUnderruns() const { return audioUnderruns_.load(); }
qulonglong Metrics::audioResyncs() const { return audioResyncs_.load(); }

QString Metrics::transport() const {
    std::lock_guard lock(textMutex_);
    return transport_;
}

QString Metrics::audioBackend() const {
    std::lock_guard lock(textMutex_);
    return audioBackend_;
}

QString Metrics::deviceName() const {
    std::lock_guard lock(textMutex_);
    return deviceName_;
}

} // namespace padmirror::diagnostics
