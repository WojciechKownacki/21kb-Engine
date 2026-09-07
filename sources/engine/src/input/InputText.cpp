#include "engine/input/InputText.hpp"

#include <cstdint>

namespace kb::input {
namespace {

[[nodiscard]] bool DecodeUnit(
    std::string_view text,
    std::size_t& offset,
    bool modified,
    char32_t& value) noexcept {
    const auto first = static_cast<std::uint8_t>(text[offset]);
    std::size_t length = 0U;
    char32_t minimum = 0U;
    if (first <= 0x7FU) {
        value = first;
        length = 1U;
    } else if ((first & 0xE0U) == 0xC0U) {
        value = first & 0x1FU;
        length = 2U;
        minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
        value = first & 0x0FU;
        length = 3U;
        minimum = 0x800U;
    } else if (!modified && (first & 0xF8U) == 0xF0U) {
        value = first & 0x07U;
        length = 4U;
        minimum = 0x10000U;
    } else {
        return false;
    }

    if (length > text.size() - offset) return false;
    for (std::size_t index = 1U; index < length; ++index) {
        const auto continuation = static_cast<std::uint8_t>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) return false;
        value = (value << 6U) | (continuation & 0x3FU);
    }
    const bool modifiedNull = modified && length == 2U && value == 0U &&
        first == 0xC0U && static_cast<std::uint8_t>(text[offset + 1U]) == 0x80U;
    if ((!modifiedNull && value < minimum) || value > 0x10FFFFU ||
        (!modified && !IsUnicodeScalar(value))) {
        return false;
    }
    offset += length;
    return true;
}

void AppendDecoded(
    char32_t codePoint,
    std::span<char32_t> output,
    Utf8DecodeResult& result) noexcept {
    if (result.codePointCount < output.size()) {
        output[result.codePointCount++] = codePoint;
    } else {
        result.truncated = true;
    }
}

} // namespace

Utf8DecodeResult DecodeUtf8(
    std::string_view text, std::span<char32_t> output) noexcept {
    Utf8DecodeResult result;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        char32_t codePoint = 0U;
        if (!DecodeUnit(text, offset, false, codePoint)) {
            result.wellFormed = false;
            return result;
        }
        AppendDecoded(codePoint, output, result);
    }
    return result;
}

Utf8DecodeResult DecodeModifiedUtf8(
    std::string_view text, std::span<char32_t> output) noexcept {
    constexpr char32_t highFirst = 0xD800U;
    constexpr char32_t highLast = 0xDBFFU;
    constexpr char32_t lowFirst = 0xDC00U;
    constexpr char32_t lowLast = 0xDFFFU;

    Utf8DecodeResult result;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        char32_t codeUnit = 0U;
        if (!DecodeUnit(text, offset, true, codeUnit)) {
            result.wellFormed = false;
            return result;
        }
        char32_t codePoint = codeUnit;
        if (codeUnit >= highFirst && codeUnit <= highLast) {
            char32_t low = 0U;
            if (offset >= text.size() || !DecodeUnit(text, offset, true, low) ||
                low < lowFirst || low > lowLast) {
                result.wellFormed = false;
                return result;
            }
            codePoint = 0x10000U + ((codeUnit - highFirst) << 10U) +
                (low - lowFirst);
        } else if (codeUnit >= lowFirst && codeUnit <= lowLast) {
            result.wellFormed = false;
            return result;
        }
        AppendDecoded(codePoint, output, result);
    }
    return result;
}

bool Utf16InputDecoder::Consume(char16_t codeUnit, char32_t& codePoint) noexcept {
    constexpr char16_t highFirst = 0xD800U;
    constexpr char16_t highLast = 0xDBFFU;
    constexpr char16_t lowFirst = 0xDC00U;
    constexpr char16_t lowLast = 0xDFFFU;

    if (codeUnit >= highFirst && codeUnit <= highLast) {
        pendingHighSurrogate_ = codeUnit;
        return false;
    }
    if (codeUnit >= lowFirst && codeUnit <= lowLast) {
        if (pendingHighSurrogate_ == 0U) return false;
        codePoint = 0x10000U +
            ((static_cast<char32_t>(pendingHighSurrogate_ - highFirst) << 10U) |
                static_cast<char32_t>(codeUnit - lowFirst));
        pendingHighSurrogate_ = 0U;
        return true;
    }

    pendingHighSurrogate_ = 0U;
    codePoint = static_cast<char32_t>(codeUnit);
    return true;
}

void Utf16InputDecoder::Reset() noexcept {
    pendingHighSurrogate_ = 0U;
}

} // namespace kb::input
