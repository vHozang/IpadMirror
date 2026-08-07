#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace padmirror::capture::usb::binary {

template <typename T>
T readLe(std::span<const std::uint8_t> bytes, std::size_t offset = 0) {
    static_assert(std::is_integral_v<T>);
    if (offset + sizeof(T) > bytes.size()) {
        throw std::out_of_range("binary read exceeds packet boundary");
    }

    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<U>(bytes[offset + index]) << (index * 8U);
    }
    return static_cast<T>(value);
}

template <typename T>
T readBe(std::span<const std::uint8_t> bytes, std::size_t offset = 0) {
    static_assert(std::is_integral_v<T>);
    if (offset + sizeof(T) > bytes.size()) {
        throw std::out_of_range("binary read exceeds packet boundary");
    }

    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value = static_cast<U>((value << 8U) | bytes[offset + index]);
    }
    return static_cast<T>(value);
}

template <typename T>
void appendLe(std::vector<std::uint8_t>& bytes, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const auto unsignedValue = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::uint8_t>((unsignedValue >> (index * 8U)) & 0xffU));
    }
}

inline void appendDoubleLe(std::vector<std::uint8_t>& bytes, double value) {
    appendLe(bytes, std::bit_cast<std::uint64_t>(value));
}

inline void writeLe32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    if (offset + sizeof(value) > bytes.size()) {
        throw std::out_of_range("binary write exceeds packet boundary");
    }
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    }
}

inline std::span<const std::uint8_t> checkedBlock(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::uint32_t expectedMagic = 0) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("truncated protocol block header");
    }
    const auto length = readLe<std::uint32_t>(bytes, offset);
    const auto magic = readLe<std::uint32_t>(bytes, offset + 4);
    if (length < 8 || offset + length > bytes.size()) {
        throw std::runtime_error("invalid protocol block length");
    }
    if (expectedMagic != 0 && magic != expectedMagic) {
        throw std::runtime_error("unexpected protocol block type");
    }
    return bytes.subspan(offset, length);
}

} // namespace padmirror::capture::usb::binary
