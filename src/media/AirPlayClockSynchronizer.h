#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace padmirror::media {

class AirPlayClockSynchronizer final {
public:
    enum class Stream {
        Video,
        Audio,
    };

    struct Result {
        std::uint64_t ptsNs = 0;
        bool rebased = false;
    };

    [[nodiscard]] Result normalize(
        Stream stream,
        std::uint64_t sourcePtsNs,
        std::uint64_t durationNs,
        std::uint64_t runningTimeNs);
    void reset();

private:
    struct StreamState {
        bool initialized = false;
        std::uint64_t sourceAnchorNs = 0;
        std::uint64_t runningAnchorNs = 0;
        std::int64_t correctionNs = 0;
        std::uint64_t lastSourcePtsNs = 0;
        std::uint64_t lastOutputPtsNs = 0;
    };

    [[nodiscard]] static std::size_t index(Stream stream);

    std::mutex mutex_;
    std::array<StreamState, 2> streams_{};
};

} // namespace padmirror::media
