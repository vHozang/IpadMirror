#include "diagnostics/FpsCounter.h"

namespace padmirror::diagnostics {

void FpsCounter::tick() {
    const auto now = Clock::now();
    std::lock_guard lock(mutex_);
    frames_.push_back(now);
    trimLocked(now);
}

double FpsCounter::framesPerSecond() const {
    const auto now = Clock::now();
    std::lock_guard lock(mutex_);
    trimLocked(now);
    if (frames_.size() < 2) {
        return 0.0;
    }
    const auto elapsed = std::chrono::duration<double>(frames_.back() - frames_.front()).count();
    return elapsed > 0.0 ? static_cast<double>(frames_.size() - 1) / elapsed : 0.0;
}

void FpsCounter::reset() {
    std::lock_guard lock(mutex_);
    frames_.clear();
}

void FpsCounter::trimLocked(Clock::time_point now) const {
    const auto threshold = now - std::chrono::seconds(2);
    while (!frames_.empty() && frames_.front() < threshold) {
        frames_.pop_front();
    }
}

} // namespace padmirror::diagnostics
