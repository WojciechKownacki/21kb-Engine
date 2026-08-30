#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

// Path and name arithmetic shared by every bake store: the loose sink, which builds a
// directory per artifact, and the pack writer, which builds one container file. Both address
// their store through the same normalisation, so a store root that one of them can open is a
// store root the other can open too.
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

// Win32 resolves an ordinary path against MAX_PATH (260 characters); only the extended-length
// prefix lifts that ceiling to 32767. A bake-store path built from names at
// kMaxBakeCacheNameBytes spends about 240 characters before the store root is even counted,
// and an ordinary path then fails in the worst possible way: the directory creates and the
// rename reports success, the file inside cannot be opened, and remove_all cannot delete it
// again. Normalising the root once makes every path derived from it extended-length,
// including the ones a store hands back to a reader.
[[nodiscard]] inline std::filesystem::path Normalize(std::filesystem::path path) {
#if defined(_WIN32)
    if (path.empty() || path.native().starts_with(LR"(\\?\)") || path.native().starts_with(LR"(\\.\)")) {
        return path;
    }
    std::error_code error;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
    if (error) {
        return path;
    }
    // An extended-length path is passed to the object manager verbatim: no '/' separators, no
    // '.' or '..' components and no trailing separator, or the final name resolves to
    // something other than the entry meant.
    const std::filesystem::path normalized = absolutePath.lexically_normal();
    std::wstring native = normalized.native();
    std::ranges::replace(native, L'/', L'\\');
    const std::size_t rootLength = normalized.root_path().native().size();
    while (native.size() > rootLength && native.back() == L'\\') {
        native.pop_back();
    }
    if (native.starts_with(LR"(\\)")) {
        return std::filesystem::path{ std::wstring{ LR"(\\?\UNC)" } + native.substr(1U) };
    }
    return std::filesystem::path{ std::wstring{ LR"(\\?\)" } + native };
#else
    return path;
#endif
}

} // namespace kb::assets::bake::store
