#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace padmirror::media {

class AudioRingBuffer {
public:
    struct PushResult {
        std::size_t acceptedBytes = 0;
        std::size_t droppedBytes = 0;
        bool resynced = false;
    };

    AudioRingBuffer(
        std::uint32_t sampleRate = 48000,
        std::uint32_t channels = 2,
        std::uint32_t bytesPerSample = 2,
        double targetMs = 10.0,
        double hardMaxMs = 40.0);

    PushResult push(std::span<const std::uint8_t> data);
    std::size_t pop(std::span<std::uint8_t> destination);
    bool waitForBytes(std::size_t minimumBytes, std::chrono::milliseconds timeout);

    void clear();
    void wakeAll();

    [[nodiscard]] std::size_t sizeBytes() const;
    [[nodiscard]] std::size_t targetBytes() const;
    [[nodiscard]] std::size_t capacityBytes() const;
    [[nodiscard]] double bufferedMilliseconds() const;
    [[nodiscard]] double millisecondsForBytes(std::size_t bytes) const;

private:
    void discardOldestLocked(std::size_t bytes);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::uint8_t> storage_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t targetBytes_ = 0;
    double bytesPerMillisecond_ = 1.0;
};

} // namespace padmirror::media
