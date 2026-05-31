#include "engine/assets/AssetId.hpp"

#include <array>
#include <charconv>

namespace kb::assets {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

} // namespace

AssetId MakeAssetId(std::string_view stableKey) noexcept {
    std::uint64_t hash = FnvOffset;
    for (const char value : stableKey) {
        hash ^= static_cast<unsigned char>(value);
        hash *= FnvPrime;
    }
    if (hash == 0) {
        hash = FnvPrime;
    }
    return AssetId{ hash };
}

std::string ToString(AssetId id) {
    std::array<char, 16> buffer{};
    char* output = buffer.data() + buffer.size();
    std::uint64_t value = id.value;
    constexpr char Digits[] = "0123456789abcdef";
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        *--output = Digits[value & 0xFU];
        value >>= 4U;
    }
    return std::string{ buffer.data(), buffer.size() };
}

bool TryParseAssetId(std::string_view text, AssetId& output) noexcept {
    if (text.empty()) {
        return false;
    }

    std::uint64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value, 16);
    if (result.ec != std::errc{} || result.ptr != last || value == 0) {
        return false;
    }
    output = AssetId{ value };
    return true;
}

} // namespace kb::assets
