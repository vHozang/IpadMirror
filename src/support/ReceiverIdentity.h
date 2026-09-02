#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace padmirror::support {

[[nodiscard]] std::array<std::uint8_t, 6> receiverHardwareAddress(
    std::string_view machineIdentity);
[[nodiscard]] std::string formatHardwareAddress(
    const std::array<std::uint8_t, 6>& address);
[[nodiscard]] std::array<char, 6> nativeHardwareAddress(
    const std::array<std::uint8_t, 6>& address);

} // namespace padmirror::support
