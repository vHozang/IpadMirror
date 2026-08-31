#include "media/AirPlayClockSynchronizer.h"

#include <algorithm>
#include <limits>

namespace padmirror::media {
namespace {

constexpr std::uint64_t kLiveLeadNs = 15'000'000;
constexpr std::int64_t kRunningPastToleranceNs = 250'000'000;
constexpr std::int64_t kRunningFutureToleranceNs = 500'000'000;
constexpr std::uint64_t kRegressionToleranceNs = 5'000'000;
// Correct only a small fraction of observed error on each packet. This keeps
// independent RTP clocks phase-locked without introducing audible or visible
// timestamp jumps; large discontinuities still take the rebase path below.
constexpr std::int64_t kMaximumCorrectionStepNs = 2'000'000;
constexpr std::int64_t kCorrectionDeadbandNs = 1'000'000;

std::int64_t difference(std::uint64_t left, std::uint64_t right) {
    if (left >= right) {
        const auto delta = left - right;
        return delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(delta);
    }
    const auto delta = right - left;
    return delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::min()
        : -static_cast<std::int64_t>(delta);
}

std::uint64_t addSigned(std::uint64_t base, std::int64_t offset) {
    if (offset >= 0) return base + static_cast<std::uint64_t>(offset);
    const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
    return magnitude >= base ? 0 : base - magnitude;
}

} // namespace

AirPlayClockSynchronizer::Result AirPlayClockSynchronizer::normalize(
    Stream stream,
    std::uint64_t sourcePtsNs,
    std::uint64_t durationNs,
    std::uint64_t runningTimeNs) {
    std::lock_guard lock(mutex_);
    auto& state = streams_[index(stream)];
    const auto fallbackDuration = stream == Stream::Video ? 16'666'667ULL : 10'000'000ULL;
    const auto safeDuration = durationNs > 0 && durationNs <= fallbackDuration * 4
        ? durationNs
        : fallbackDuration;

    if (sourcePtsNs == 0 && state.initialized) {
        sourcePtsNs = state.lastSourcePtsNs + safeDuration;
    }
    if (!state.initialized) {
        state.sourceAnchorNs = sourcePtsNs;
        state.runningAnchorNs = runningTimeNs + kLiveLeadNs;
        state.initialized = true;
        state.correctionNs = 0;
    }

    const auto sourceDelta = difference(sourcePtsNs, state.sourceAnchorNs);
    const auto baseOutput = addSigned(state.runningAnchorNs, sourceDelta);
    auto output = addSigned(baseOutput, state.correctionNs);
    bool rebased = false;

    const auto relativeToNow = difference(output, runningTimeNs);
    const bool regressed = state.lastSourcePtsNs != 0 &&
        sourcePtsNs + kRegressionToleranceNs < state.lastSourcePtsNs;
    const bool outsideLiveWindow = relativeToNow < -kRunningPastToleranceNs ||
        relativeToNow > kRunningFutureToleranceNs;
    if (regressed || outsideLiveWindow) {
        state.sourceAnchorNs = sourcePtsNs;
        state.runningAnchorNs = runningTimeNs + kLiveLeadNs;
        state.correctionNs = 0;
        output = state.runningAnchorNs;
        rebased = true;
    } else if (state.lastOutputPtsNs != 0) {
        // The source timestamp progression and the local running time are
        // independent clocks. Nudge the correction toward the live edge,
        // rather than repeatedly moving the anchor (which would create A/V
        // jumps during normal clock drift).
        const auto error = difference(output, runningTimeNs + kLiveLeadNs);
        if (error > kCorrectionDeadbandNs || error < -kCorrectionDeadbandNs) {
            const auto step = std::clamp(
                -error,
                -kMaximumCorrectionStepNs,
                kMaximumCorrectionStepNs);
            state.correctionNs += step;
            output = addSigned(baseOutput, state.correctionNs);
        }
    }

    if (state.lastOutputPtsNs != 0 && output <= state.lastOutputPtsNs) {
        output = state.lastOutputPtsNs + safeDuration;
    }
    // Keep correction bounded so a long-running stream cannot accumulate an
    // offset large enough to mask a genuine discontinuity.
    state.correctionNs = std::clamp(
        state.correctionNs,
        -kRunningPastToleranceNs,
        kRunningFutureToleranceNs);
    state.lastSourcePtsNs = sourcePtsNs;
    state.lastOutputPtsNs = output;
    return {output, rebased};
}

void AirPlayClockSynchronizer::reset() {
    std::lock_guard lock(mutex_);
    streams_ = {};
}

std::size_t AirPlayClockSynchronizer::index(Stream stream) {
    return stream == Stream::Video ? 0U : 1U;
}

} // namespace padmirror::media
