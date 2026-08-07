#pragma once

#include <cstddef>

namespace padmirror::media {

class LiveEdgeController {
public:
    struct AudioDecision {
        bool warning = false;
        bool resync = false;
        double keepMilliseconds = 0.0;
    };

    explicit LiveEdgeController(
        double targetAudioMs = 10.0,
        double warningAudioMs = 25.0,
        double hardMaxAudioMs = 40.0);

    [[nodiscard]] bool shouldDropVideo(std::size_t queuedFrames) const;
    [[nodiscard]] AudioDecision evaluateAudio(double bufferedMilliseconds) const;

    [[nodiscard]] double targetAudioMs() const;
    [[nodiscard]] double hardMaxAudioMs() const;

private:
    double targetAudioMs_;
    double warningAudioMs_;
    double hardMaxAudioMs_;
};

} // namespace padmirror::media
