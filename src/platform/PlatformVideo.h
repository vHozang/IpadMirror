#pragma once

#include <string>
#include <vector>

namespace padmirror::platform {

struct VideoBackend {
    std::vector<std::string> decoderCandidates;
    std::vector<std::string> sinkCandidates;
    bool hardwareOnly = true;
};

[[nodiscard]] VideoBackend videoBackend();

namespace detail {
[[nodiscard]] VideoBackend nativeVideoBackend();
}

} // namespace padmirror::platform
