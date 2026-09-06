#pragma once

#include "engine/platform/FileSystemPath.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>

// Path and name arithmetic shared by every bake store.
namespace kb::assets::bake::store {

[[nodiscard]] constexpr bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] constexpr char ToLowerAscii(char character) noexcept {
    return (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
}

[[nodiscard]] inline bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) noexcept {
    return std::ranges::equal(lhs, rhs, [](char left, char right) noexcept {
        return ToLowerAscii(left) == ToLowerAscii(right);
    });
}

// A bake-store path built from names at kMaxBakeCacheNameBytes spends about 240 characters before
// the store root is even counted, so every store addresses its root through the platform's
// extended-length normalisation: the loose sink, which builds a directory per artifact, and the
// pack writer, which builds one container file, then agree on what a path means, and a store root
// one of them can open is a store root the other can open too.
[[nodiscard]] inline std::filesystem::path Normalize(std::filesystem::path path) {
    return kb::platform::ExtendedLengthPath(std::move(path));
}

} // namespace kb::assets::bake::store
