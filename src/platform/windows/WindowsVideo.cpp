#include "platform/PlatformVideo.h"

namespace padmirror::platform::detail {

VideoBackend nativeVideoBackend() {
    return {{"d3d11h264dec"}, {"d3d11videosink"}, true};
}

} // namespace padmirror::platform::detail
