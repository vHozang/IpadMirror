#include "platform/PlatformAudio.h"

namespace padmirror::platform::detail {

AudioBackend nativeAudioBackend() {
    return {{"wasapi2sink", "wasapisink"}, "WASAPI"};
}

} // namespace padmirror::platform::detail
