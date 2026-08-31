#include "media/LiveEdgeController.h"

#include <cassert>

using padmirror::media::LiveEdgeController;

int main() {
    LiveEdgeController controller(10.0, 25.0, 40.0);
    assert(!controller.shouldDropVideo(1));
    assert(controller.shouldDropVideo(2));
    assert(!controller.evaluateAudio(20.0).warning);
    assert(controller.evaluateAudio(30.0).warning);
    const auto hardLimit = controller.evaluateAudio(41.0);
    assert(hardLimit.resync);
    assert(hardLimit.keepMilliseconds == 10.0);

    LiveEdgeController gamingAudio(25.0, 80.0, 180.0);
    assert(!gamingAudio.evaluateAudio(179.0).resync);
    const auto gamingCatchUp = gamingAudio.evaluateAudio(181.0);
    assert(gamingCatchUp.resync);
    assert(gamingCatchUp.keepMilliseconds == 25.0);

    LiveEdgeController mediaAudio(50.0, 120.0, 250.0);
    assert(!mediaAudio.evaluateAudio(249.0).resync);
    assert(mediaAudio.evaluateAudio(251.0).resync);
    return 0;
}
