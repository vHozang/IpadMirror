#include "platform/PlatformPower.h"

#include <windows.h>
#include <mmsystem.h>

#include <mutex>

namespace padmirror::platform {
namespace {

std::mutex powerMutex;
bool streamingPowerActive = false;
bool timerPeriodActive = false;
bool priorityChanged = false;
DWORD previousPriorityClass = NORMAL_PRIORITY_CLASS;

} // namespace

void setGamingPowerActive(bool active) {
    std::lock_guard lock(powerMutex);
    if (streamingPowerActive == active) return;

    streamingPowerActive = active;
    if (active) {
        // A 1 ms timer period prevents 60 Hz presentation and WASAPI work from
        // being grouped onto the default coarse Windows scheduler tick.
        timerPeriodActive = timeBeginPeriod(1) == TIMERR_NOERROR;
        previousPriorityClass = GetPriorityClass(GetCurrentProcess());
        if (previousPriorityClass == 0) previousPriorityClass = NORMAL_PRIORITY_CLASS;
        if (previousPriorityClass == NORMAL_PRIORITY_CLASS ||
            previousPriorityClass == BELOW_NORMAL_PRIORITY_CLASS ||
            previousPriorityClass == IDLE_PRIORITY_CLASS) {
            priorityChanged = SetPriorityClass(
                GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS) != FALSE;
        }
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        return;
    }

    SetThreadExecutionState(ES_CONTINUOUS);
    if (priorityChanged) SetPriorityClass(GetCurrentProcess(), previousPriorityClass);
    if (timerPeriodActive) timeEndPeriod(1);
    priorityChanged = false;
    timerPeriodActive = false;
}

} // namespace padmirror::platform
