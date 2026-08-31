#pragma once

#include "core/MirrorTypes.h"

namespace padmirror::core {

class IMirrorBackend {
public:
    virtual ~IMirrorBackend() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual MirrorState state() const = 0;
    [[nodiscard]] virtual MirrorHealth health() const = 0;
};

} // namespace padmirror::core
