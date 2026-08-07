#include "platform/PlatformPower.h"

#include <windows.h>

namespace padmirror::platform {

void setGamingPowerActive(bool active) {
    SetThreadExecutionState(active
        ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
        : ES_CONTINUOUS);
}

} // namespace padmirror::platform
