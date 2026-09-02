#include "support/ReceiverIdentity.h"

#include <cassert>

int main() {
    using padmirror::support::formatHardwareAddress;
    using padmirror::support::nativeHardwareAddress;
    using padmirror::support::receiverHardwareAddress;

    const auto first = receiverHardwareAddress("windows-host-01");
    const auto second = receiverHardwareAddress("windows-host-01");
    const auto other = receiverHardwareAddress("windows-host-02");

    assert(first == second);
    assert(first != other);
    assert((first[0] & 0x01U) == 0U);
    assert((first[0] & 0x02U) == 0x02U);
    assert(formatHardwareAddress(first).size() == 17);
    const auto native = nativeHardwareAddress(first);
    for (std::size_t index = 0; index < first.size(); ++index) {
        assert(static_cast<unsigned char>(native[index]) == first[index]);
    }
    return 0;
}
