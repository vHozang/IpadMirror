#include "support/ReceiverIdentity.h"

#include <cstdio>

namespace padmirror::support {

std::array<std::uint8_t, 6> receiverHardwareAddress(
    std::string_view machineIdentity) {
    if (machineIdentity.empty()) {
        machineIdentity = "PadMirror";
    }

    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : machineIdentity) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }

    std::array<std::uint8_t, 6> address{};
    for (std::size_t index = 0; index < address.size(); ++index) {
        address[index] = static_cast<std::uint8_t>((hash >> (8U * index)) & 0xffU);
    }
    address[0] = static_cast<std::uint8_t>((address[0] & ~0x01U) | 0x02U);
    return address;
}

std::string formatHardwareAddress(const std::array<std::uint8_t, 6>& address) {
    char text[18]{};
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                  address[0], address[1], address[2],
                  address[3], address[4], address[5]);
    return text;
}

std::array<char, 6> nativeHardwareAddress(
    const std::array<std::uint8_t, 6>& address) {
    std::array<char, 6> native{};
    for (std::size_t index = 0; index < address.size(); ++index) {
        native[index] = static_cast<char>(address[index]);
    }
    return native;
}

} // namespace padmirror::support
