#include "platform/PlatformAudio.h"

namespace padmirror::platform::detail {

AudioBackend nativeAudioBackend() {
    return {{"osxaudiosink"}, "CoreAudio"};
}

} // namespace padmirror::platform::detail
