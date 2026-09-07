#pragma once

#include <cstddef>
#include <cstdint>
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

// Decodes a complete UTF-8 string into Unicode scalar values. If output is too
// small, the prefix that fits is returned while the entire source is still
// validated. Malformed input never produces a replacement character silently.
[[nodiscard]] Utf8DecodeResult DecodeUtf8(std::string_view text, std::span<char32_t> output) noexcept;

// GameActivity's GameTextInput channel uses Java modified UTF-8: U+0000 is
// encoded as C0 80 and supplementary characters as UTF-16 surrogate pairs.
// The result is normalized to Unicode scalar values for InputDeviceState.
[[nodiscard]] Utf8DecodeResult DecodeModifiedUtf8(std::string_view text, std::span<char32_t> output) noexcept;

// Stateful because Win32 WM_CHAR delivers UTF-16 code units one message at a
// time. A scalar is produced only after a complete, valid sequence arrives.
class Utf16InputDecoder {
  public:
    [[nodiscard]] bool Consume(char16_t codeUnit, char32_t& codePoint) noexcept;
    void Reset() noexcept;

  private:
    char16_t pendingHighSurrogate_ = 0U;
};

} // namespace kb::input
