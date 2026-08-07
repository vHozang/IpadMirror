#include "media/AudioRingBuffer.h"

#include <cassert>
#include <cstdint>
#include <vector>

using padmirror::media::AudioRingBuffer;

int main() {
    AudioRingBuffer buffer(1000, 1, 1, 2.0, 4.0);
    const std::vector<std::uint8_t> first = {1, 2, 3};
    const auto firstResult = buffer.push(first);
    assert(firstResult.droppedBytes == 0);
    assert(buffer.sizeBytes() == 3);

    const std::vector<std::uint8_t> second = {4, 5, 6};
    const auto secondResult = buffer.push(second);
    assert(secondResult.resynced);
    assert(secondResult.droppedBytes == 3);
    assert(buffer.sizeBytes() == 3);

    std::vector<std::uint8_t> output(3);
    assert(buffer.pop(output) == 3);
    assert((output == std::vector<std::uint8_t>{4, 5, 6}));

    const std::vector<std::uint8_t> oversized = {7, 8, 9, 10, 11, 12};
    const auto oversizedResult = buffer.push(oversized);
    assert(oversizedResult.resynced);
    assert(oversizedResult.droppedBytes == 2);
    assert(buffer.sizeBytes() == 4);
    output.resize(4);
    assert(buffer.pop(output) == 4);
    assert((output == std::vector<std::uint8_t>{9, 10, 11, 12}));
    return 0;
}
