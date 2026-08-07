#pragma once

#include <string>
#include <vector>

namespace padmirror::platform {

struct AudioBackend {
    std::vector<std::string> sinkCandidates;
    std::string displayName;
};

[[nodiscard]] AudioBackend audioBackend();

namespace detail {
[[nodiscard]] AudioBackend nativeAudioBackend();
}

} // namespace padmirror::platform
