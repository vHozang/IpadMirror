#include "platform/PlatformVideo.h"

namespace padmirror::platform {

VideoBackend videoBackend() {
    return detail::nativeVideoBackend();
}

#if !defined(_WIN32) && !defined(__APPLE__)
namespace detail {
VideoBackend nativeVideoBackend() {
    return {{"vah264dec", "vaapih264dec", "avdec_h264"}, {"glimagesink", "autovideosink"}, false};
}
} // namespace detail
#endif

} // namespace padmirror::platform
