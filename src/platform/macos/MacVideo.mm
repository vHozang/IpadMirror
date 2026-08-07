#include "platform/PlatformVideo.h"

namespace padmirror::platform::detail {

VideoBackend nativeVideoBackend() {
    return {{"vtdec_hw", "vtdec"}, {"glimagesink", "osxvideosink"}, true};
}

} // namespace padmirror::platform::detail
