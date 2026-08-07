#include "capture/usb/BinaryIO.h"
#include "capture/usb/QuickTimeProtocol.h"

#include <algorithm>
#include <cassert>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using padmirror::capture::usb::QuickTimeProtocol;
using namespace padmirror::capture::usb::binary;

namespace {

std::vector<std::uint8_t> readFixture(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void validateReferenceFixtures(const std::filesystem::path& root) {
    QuickTimeProtocol protocol;
    std::vector<std::vector<std::uint8_t>> writes;
    protocol.setWriter([&writes](std::span<const std::uint8_t> bytes) {
        writes.emplace_back(bytes.begin(), bytes.end());
        return true;
    });

    const auto cwpaRequest = readFixture(root / "cwpa-request1");
    assert(cwpaRequest.size() >= 4);
    assert(protocol.processFrame(std::span(cwpaRequest).subspan(4)));
    assert(writes.size() == 4);
    assert(writes[0] == readFixture(root / "asyn-hpd1"));
    assert(writes[1] == readFixture(root / "asyn-hpd1"));
    assert(writes[2].size() == 28);
    assert(readLe<std::uint32_t>(writes[2], 4) == 0x72706c79);
    assert(readLe<std::uint64_t>(writes[2], 8) == readLe<std::uint64_t>(cwpaRequest, 20));
    assert(readLe<std::uint64_t>(writes[2], 20) == readLe<std::uint64_t>(cwpaRequest, 28) + 1000);
    const auto hpaFixture = readFixture(root / "asyn-hpa1");
    assert(writes[3].size() == hpaFixture.size());
    assert(readLe<std::uint32_t>(writes[3], 4) == 0x6173796e);
    assert(readLe<std::uint64_t>(writes[3], 8) == readLe<std::uint64_t>(cwpaRequest, 28));
    const auto mismatch = std::mismatch(
        writes[3].begin() + 16, writes[3].end(), hpaFixture.begin() + 16);
    if (mismatch.first != writes[3].end()) {
        std::cerr << "HPA1 fixture mismatch at byte "
                  << std::distance(writes[3].begin(), mismatch.first)
                  << ": actual=" << static_cast<unsigned>(*mismatch.first)
                  << " expected=" << static_cast<unsigned>(*mismatch.second) << '\n';
    }
    assert(std::equal(writes[3].begin() + 16, writes[3].end(), hpaFixture.begin() + 16));

    writes.clear();
    const auto skewRequest = readFixture(root / "skew-request");
    assert(skewRequest.size() >= 4);
    assert(protocol.processFrame(std::span(skewRequest).subspan(4)));
    assert(writes.size() == 1);
    assert(writes[0] == readFixture(root / "skew-reply"));
}

} // namespace

int main() {
    QuickTimeProtocol protocol;
    std::vector<std::uint8_t> written;
    bool ready = false;
    protocol.setWriter([&written](std::span<const std::uint8_t> bytes) {
        written.assign(bytes.begin(), bytes.end());
        return true;
    });
    protocol.setReadyHandler([&ready] { ready = true; });

    std::vector<std::uint8_t> pingPayload;
    appendLe(pingPayload, std::uint32_t{0x70696e67});
    appendLe(pingPayload, std::uint64_t{0x0000000100000000ULL});

    assert(protocol.processFrame(pingPayload));
    assert(ready);
    assert(written.size() == 16);
    assert(readLe<std::uint32_t>(written) == 16);
    assert(readLe<std::uint32_t>(written, 4) == 0x70696e67);

    std::vector<std::uint8_t> skewRequest;
    appendLe(skewRequest, std::uint32_t{0x73796e63});
    appendLe(skewRequest, std::uint64_t{1});
    appendLe(skewRequest, std::uint32_t{0x736b6577});
    appendLe(skewRequest, std::uint64_t{0x1234});
    assert(protocol.processFrame(skewRequest));
    assert(written.size() == 28);
    assert(readLe<std::uint32_t>(written, 4) == 0x72706c79);
    assert(readLe<std::uint64_t>(written, 8) == 0x1234);
    const auto skew = std::bit_cast<double>(readLe<std::uint64_t>(written, 20));
    assert(std::abs(skew - 48000.0) < 0.001);

    if (const auto* fixtures = std::getenv("PADMIRROR_IOSCREEN_FIXTURES")) {
        validateReferenceFixtures(fixtures);
    }
    return 0;
}
