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
    return 0;
}
