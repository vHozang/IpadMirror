#include "diagnostics/Metrics.h"

namespace padmirror::diagnostics {

Metrics::Metrics(QObject* parent)
    : QObject(parent) {
    refreshTimer_.setInterval(250);
    connect(&refreshTimer_, &QTimer::timeout, this, &Metrics::changed);
    refreshTimer_.start();
}

void Metrics::sourceFrame() {
    sourceFrames_.fetch_add(1);
    sourceCounter_.tick();
}
void Metrics::decodedFrame() {
    decodedFrames_.fetch_add(1);
    decodeCounter_.tick();
}
void Metrics::renderedFrame() {
    renderedFrames_.fetch_add(1);
    renderCounter_.tick();
}
void Metrics::videoTimestamp(std::uint64_t ptsNs) {
    if (ptsNs != 0) latestVideoPtsNs_.store(ptsNs);
}
void Metrics::audioPacket(std::uint64_t ptsNs) {
    audioPackets_.fetch_add(1);
    if (ptsNs != 0) latestAudioPtsNs_.store(ptsNs);
}
void Metrics::droppedFrame(std::uint64_t count) { droppedFrames_.fetch_add(count); }
void Metrics::audioUnderrun() { audioUnderruns_.fetch_add(1); }
void Metrics::audioResync() { audioResyncs_.fetch_add(1); }
void Metrics::setAudioBufferMs(double value) {
    audioBufferMs_.store(value);
    audioBufferValid_.store(true);
}
void Metrics::clearAudioBuffer() {
    audioBufferMs_.store(0.0);
    audioBufferValid_.store(false);
}
void Metrics::setActiveEpoch(std::uint64_t value) { activeEpoch_.store(value); }

void Metrics::setTransport(const QString& value) {
    std::lock_guard lock(textMutex_);
    transport_ = value;
}

void Metrics::setTransportState(const QString& value) {
    std::lock_guard lock(textMutex_);
    transportState_ = value;
}

void Metrics::setHealthDetail(const QString& value) {
    std::lock_guard lock(textMutex_);
    healthDetail_ = value;
}

void Metrics::setAudioBackend(const QString& value) {
    std::lock_guard lock(textMutex_);
    audioBackend_ = value;
}

void Metrics::setDeviceName(const QString& value) {
    std::lock_guard lock(textMutex_);
    deviceName_ = value;
}

void Metrics::setVideoFormat(const QString& codec, int width, int height) {
    {
        std::lock_guard lock(textMutex_);
        videoCodec_ = codec;
    }
    videoWidth_.store(width);
    videoHeight_.store(height);
}

void Metrics::setRequestedQuality(const QString& value) {
    std::lock_guard lock(textMutex_);
    requestedQuality_ = value;
}

void Metrics::setLastDiscontinuity(const QString& value) {
    std::lock_guard lock(textMutex_);
    lastDiscontinuity_ = value;
}

void Metrics::setQualityStatus(const QString& value) {
    std::lock_guard lock(textMutex_);
    qualityStatus_ = value;
}

void Metrics::reset() {
    sourceCounter_.reset();
    decodeCounter_.reset();
    renderCounter_.reset();
    sourceFrames_.store(0);
    decodedFrames_.store(0);
    renderedFrames_.store(0);
    droppedFrames_.store(0);
    audioPackets_.store(0);
    audioUnderruns_.store(0);
    audioResyncs_.store(0);
    latestVideoPtsNs_.store(0);
    latestAudioPtsNs_.store(0);
    audioBufferMs_.store(0.0);
    audioBufferValid_.store(false);
    activeEpoch_.store(0);
    videoWidth_.store(0);
    videoHeight_.store(0);
    {
        std::lock_guard lock(textMutex_);
        videoCodec_ = QStringLiteral("Waiting");
        lastDiscontinuity_.clear();
        qualityStatus_.clear();
    }
    emit changed();
}

double Metrics::sourceFps() const { return sourceCounter_.framesPerSecond(); }
double Metrics::decodeFps() const { return decodeCounter_.framesPerSecond(); }
double Metrics::renderFps() const { return renderCounter_.framesPerSecond(); }
qulonglong Metrics::sourceFrames() const { return sourceFrames_.load(); }
qulonglong Metrics::decodedFrames() const { return decodedFrames_.load(); }
qulonglong Metrics::renderedFrames() const { return renderedFrames_.load(); }
qulonglong Metrics::droppedFrames() const { return droppedFrames_.load(); }
double Metrics::audioBufferMs() const { return audioBufferMs_.load(); }
bool Metrics::audioBufferValid() const { return audioBufferValid_.load(); }
qulonglong Metrics::audioPackets() const { return audioPackets_.load(); }
qulonglong Metrics::audioUnderruns() const { return audioUnderruns_.load(); }
qulonglong Metrics::audioResyncs() const { return audioResyncs_.load(); }
double Metrics::avSyncOffsetMs() const {
    const auto video = latestVideoPtsNs_.load();
    const auto audio = latestAudioPtsNs_.load();
    if (video == 0 || audio == 0) return 0.0;
    return static_cast<double>(static_cast<std::int64_t>(audio) -
        static_cast<std::int64_t>(video)) / 1'000'000.0;
}
bool Metrics::avSyncValid() const {
    return latestVideoPtsNs_.load() != 0 && latestAudioPtsNs_.load() != 0;
}

QString Metrics::transport() const {
    std::lock_guard lock(textMutex_);
    return transport_;
}

QString Metrics::transportState() const {
    std::lock_guard lock(textMutex_);
    return transportState_;
}

QString Metrics::healthDetail() const {
    std::lock_guard lock(textMutex_);
    return healthDetail_;
}

QString Metrics::audioBackend() const {
    std::lock_guard lock(textMutex_);
    return audioBackend_;
}

QString Metrics::deviceName() const {
    std::lock_guard lock(textMutex_);
    return deviceName_;
}

QString Metrics::videoCodec() const {
    std::lock_guard lock(textMutex_);
    return videoCodec_;
}

QString Metrics::sourceResolution() const {
    const auto width = videoWidth_.load();
    const auto height = videoHeight_.load();
    return width > 0 && height > 0
        ? QStringLiteral("%1x%2").arg(width).arg(height)
        : QStringLiteral("Waiting");
}

QString Metrics::requestedQuality() const {
    std::lock_guard lock(textMutex_);
    return requestedQuality_;
}

qulonglong Metrics::activeEpoch() const { return activeEpoch_.load(); }

QString Metrics::lastDiscontinuity() const {
    std::lock_guard lock(textMutex_);
    return lastDiscontinuity_;
}

QString Metrics::qualityStatus() const {
    std::lock_guard lock(textMutex_);
    return qualityStatus_;
}

int Metrics::videoWidth() const { return videoWidth_.load(); }
int Metrics::videoHeight() const { return videoHeight_.load(); }

} // namespace padmirror::diagnostics
