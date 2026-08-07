#include "platform/PlatformPower.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

namespace padmirror::platform {
namespace {
IOPMAssertionID assertionId = kIOPMNullAssertionID;
}

void setGamingPowerActive(bool active) {
    if (active && assertionId == kIOPMNullAssertionID) {
        IOPMAssertionCreateWithName(
            kIOPMAssertionTypeNoDisplaySleep,
            kIOPMAssertionLevelOn,
            CFSTR("PadMirror Gaming Mode"),
            &assertionId);
    } else if (!active && assertionId != kIOPMNullAssertionID) {
        IOPMAssertionRelease(assertionId);
        assertionId = kIOPMNullAssertionID;
    }
}

} // namespace padmirror::platform
