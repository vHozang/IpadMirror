#pragma once

#include "core/MirrorTypes.h"

namespace padmirror::core {

class IMediaIngress {
public:
    virtual ~IMediaIngress() = default;

    virtual bool pushVideo(EncodedVideoPacket packet) = 0;
    virtual bool pushAudio(EncodedAudioPacket packet) = 0;
    virtual std::uint64_t signalDiscontinuity(DiscontinuityReason reason) = 0;
    virtual void endOfStream() = 0;
};

} // namespace padmirror::core
