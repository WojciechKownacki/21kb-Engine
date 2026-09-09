#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace kb::input {

[[nodiscard]] constexpr bool IsUnicodeScalar(char32_t value) noexcept {
    return value <= 0x10FFFFU && (value < 0xD800U || value > 0xDFFFU);
}

struct Utf8DecodeResult {
    std::size_t codePointCount = 0U;
    bool wellFormed = true;
    bool truncated = false;
};

[[nodiscard]] Utf8DecodeResult DecodeUtf8(
    std::string_view text, std::span<char32_t> output) noexcept;
[[nodiscard]] Utf8DecodeResult DecodeModifiedUtf8(
    std::string_view text, std::span<char32_t> output) noexcept;

class Utf16InputDecoder {
public:
    [[nodiscard]] bool Consume(char16_t codeUnit, char32_t& codePoint) noexcept;
    void Reset() noexcept;

private:
    char16_t pendingHighSurrogate_ = 0U;
};

} // namespace kb::input
