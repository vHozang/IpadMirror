#include "media/LiveEdgeController.h"

#include <algorithm>

namespace padmirror::media {

LiveEdgeController::LiveEdgeController(
    double targetAudioMs,
    double warningAudioMs,
    double hardMaxAudioMs)
    : targetAudioMs_(std::max(1.0, targetAudioMs)),
      warningAudioMs_(std::max(targetAudioMs_, warningAudioMs)),
      hardMaxAudioMs_(std::max(warningAudioMs_, hardMaxAudioMs)) {}

bool LiveEdgeController::shouldDropVideo(std::size_t queuedFrames) const {
    return queuedFrames > 1;
}

LiveEdgeController::AudioDecision LiveEdgeController::evaluateAudio(double bufferedMilliseconds) const {
    return {
        .warning = bufferedMilliseconds > warningAudioMs_,
        .resync = bufferedMilliseconds > hardMaxAudioMs_,
        .keepMilliseconds = targetAudioMs_,
    };
}

double LiveEdgeController::targetAudioMs() const {
    return targetAudioMs_;
}

double LiveEdgeController::hardMaxAudioMs() const {
    return hardMaxAudioMs_;
}

} // namespace padmirror::media
