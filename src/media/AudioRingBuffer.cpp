#include "media/AudioRingBuffer.h"

#include <algorithm>
#include <cmath>

namespace padmirror::media {

AudioRingBuffer::AudioRingBuffer(
    std::uint32_t sampleRate,
    std::uint32_t channels,
    std::uint32_t bytesPerSample,
    double targetMs,
    double hardMaxMs) {
    bytesPerMillisecond_ =
        static_cast<double>(sampleRate) * static_cast<double>(channels) *
        static_cast<double>(bytesPerSample) / 1000.0;
    targetBytes_ = static_cast<std::size_t>(std::ceil(bytesPerMillisecond_ * targetMs));
    const auto capacity = static_cast<std::size_t>(std::ceil(bytesPerMillisecond_ * hardMaxMs));
    storage_.resize(std::max<std::size_t>(capacity, targetBytes_));
}

AudioRingBuffer::PushResult AudioRingBuffer::push(std::span<const std::uint8_t> data) {
    std::lock_guard lock(mutex_);
    PushResult result;
    if (data.empty() || storage_.empty()) {
        return result;
    }

    if (data.size() >= storage_.size()) {
        result.droppedBytes = size_ + data.size() - storage_.size();
        result.resynced = result.droppedBytes != 0;
        head_ = 0;
        size_ = storage_.size();
        const auto tail = data.last(storage_.size());
        std::copy(tail.begin(), tail.end(), storage_.begin());
        result.acceptedBytes = storage_.size();
        condition_.notify_all();
        return result;
    }

    if (size_ + data.size() > storage_.size()) {
        const auto liveEdgeBytes = std::min(
            storage_.size(), std::max(targetBytes_, data.size()));
        const auto discard = size_ + data.size() - liveEdgeBytes;
        discardOldestLocked(discard);
        result.droppedBytes = discard;
        result.resynced = true;
    }

    auto writeIndex = (head_ + size_) % storage_.size();
    const auto first = std::min(data.size(), storage_.size() - writeIndex);
    std::copy_n(data.begin(), first, storage_.begin() + static_cast<std::ptrdiff_t>(writeIndex));
    if (first < data.size()) {
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(first), data.end(), storage_.begin());
    }
    size_ += data.size();
    result.acceptedBytes = data.size();
    condition_.notify_all();
    return result;
}

std::size_t AudioRingBuffer::pop(std::span<std::uint8_t> destination) {
    std::lock_guard lock(mutex_);
    const auto count = std::min(destination.size(), size_);
    if (count == 0) {
        return 0;
    }

    const auto first = std::min(count, storage_.size() - head_);
    std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(head_), first, destination.begin());
    if (first < count) {
        std::copy_n(storage_.begin(), count - first, destination.begin() + static_cast<std::ptrdiff_t>(first));
    }
    discardOldestLocked(count);
    return count;
}

bool AudioRingBuffer::waitForBytes(std::size_t minimumBytes, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, timeout, [this, minimumBytes] {
        return size_ >= minimumBytes;
    });
}

void AudioRingBuffer::clear() {
    std::lock_guard lock(mutex_);
    head_ = 0;
    size_ = 0;
}

void AudioRingBuffer::wakeAll() {
    condition_.notify_all();
}

std::size_t AudioRingBuffer::sizeBytes() const {
    std::lock_guard lock(mutex_);
    return size_;
}

std::size_t AudioRingBuffer::targetBytes() const {
    return targetBytes_;
}

std::size_t AudioRingBuffer::capacityBytes() const {
    return storage_.size();
}

double AudioRingBuffer::bufferedMilliseconds() const {
    std::lock_guard lock(mutex_);
    return static_cast<double>(size_) / bytesPerMillisecond_;
}

double AudioRingBuffer::millisecondsForBytes(std::size_t bytes) const {
    return static_cast<double>(bytes) / bytesPerMillisecond_;
}

void AudioRingBuffer::discardOldestLocked(std::size_t bytes) {
    const auto count = std::min(bytes, size_);
    head_ = storage_.empty() ? 0 : (head_ + count) % storage_.size();
    size_ -= count;
}

} // namespace padmirror::media
