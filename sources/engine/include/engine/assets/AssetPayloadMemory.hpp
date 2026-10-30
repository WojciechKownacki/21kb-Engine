#pragma once

#include <cstdint>
#include <string>

namespace kb::assets {

// Owned string capacity only; the containing object's size already includes inline storage.
[[nodiscard]] inline std::uint64_t StringBufferBytes(const std::string& value) noexcept {
    const auto data = reinterpret_cast<std::uintptr_t>(value.data());
    const auto object = reinterpret_cast<std::uintptr_t>(&value);
    return value.capacity() == 0U || (data >= object && data < object + sizeof(value)) ? 0U : value.capacity() + 1U;
}

} // namespace kb::assets
