#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>

namespace padmirror::diagnostics {

class FpsCounter {
public:
    void tick();
    [[nodiscard]] double framesPerSecond() const;
    void reset();

private:
    using Clock = std::chrono::steady_clock;
    void trimLocked(Clock::time_point now) const;

    mutable std::mutex mutex_;
    mutable std::deque<Clock::time_point> frames_;
};

} // namespace padmirror::diagnostics
