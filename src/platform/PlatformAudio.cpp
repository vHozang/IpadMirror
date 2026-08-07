#include "platform/PlatformAudio.h"

namespace padmirror::platform {

AudioBackend audioBackend() {
    return detail::nativeAudioBackend();
}

#if !defined(_WIN32) && !defined(__APPLE__)
namespace detail {
AudioBackend nativeAudioBackend() {
    return {{"pipewiresink", "pulsesink", "autoaudiosink"}, "Linux audio"};
}
} // namespace detail
#endif

} // namespace padmirror::platform
