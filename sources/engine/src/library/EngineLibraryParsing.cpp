#include "engine/library/EngineLibraryParsing.hpp"

#include <array>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <clocale>
#else
#include <locale.h>
#endif

namespace kb::library {
namespace {

[[nodiscard]] bool IsHexDigit(char character) noexcept {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

[[nodiscard]] std::uint8_t HexNibble(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    return static_cast<std::uint8_t>(character - 'A' + 10);
}

[[nodiscard]] bool TryParseHexByte(char high, char low, std::uint8_t& outByte) noexcept {
    if (!IsHexDigit(high) || !IsHexDigit(low)) {
        return false;
    }
    outByte = static_cast<std::uint8_t>((HexNibble(high) << 4U) | HexNibble(low));
    return true;
}

// Strictly unsigned decimal digits — from_chars alone would also accept a
// leading '-' (e.g. "-1" as a 2-character field), which has no place in an
// ISO 8601 year/month/day component; this is checked explicitly rather
// than relying on std::chrono::year_month_day::ok() to reject the
// resulting out-of-range value after an unsigned-cast truncation.
[[nodiscard]] bool TryParseDigits(std::string_view text, int& outValue) noexcept {
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    const std::from_chars_result conversion = std::from_chars(text.data(), text.data() + text.size(), outValue);
    return conversion.ec == std::errc{} && conversion.ptr == text.data() + text.size();
}

// std::from_chars<double> is not available in Apple's shipped libc++ as of
// Xcode 16.4 ("call to deleted function" - confirmed empirically, with
// only std::from_chars's INTEGRAL overloads visible as candidates
// regardless of deployment target or availability macros, so this is a
// genuine gap in that library, not a gated one). std::strtod/std::atof/
// std::stringstream were deliberately ruled out originally because their
// behaviour depends on the PROCESS-GLOBAL locale (this file's own top
// comment). The locale-EXPLICIT C library entry points - strtod_l on
// POSIX, _strtod_l on MSVC - give the identical correctly-rounded
// conversion without touching or depending on that global state: a bound,
// per-call "C" locale instead, upholding the same invariant a different
// way.
[[nodiscard]] double ParseDoubleInvariant(const char* text, char** endPtr) noexcept {
#if defined(_WIN32)
    _locale_t invariantLocale = _create_locale(LC_ALL, "C");
    const double value = _strtod_l(text, endPtr, invariantLocale);
    _free_locale(invariantLocale);
    return value;
#else
    locale_t invariantLocale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
    const double value = strtod_l(text, endPtr, invariantLocale);
    freelocale(invariantLocale);
    return value;
#endif
}

} // namespace

bool TryParseInt64(std::string_view text, std::int64_t& outValue) noexcept {
    if (text.empty()) {
        return false;
    }
    std::int64_t value = 0;
    const std::from_chars_result conversion = std::from_chars(text.data(), text.data() + text.size(), value);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
        return false;
    }
    outValue = value;
    return true;
}

bool TryParseUInt64(std::string_view text, std::uint64_t& outValue) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    const std::from_chars_result conversion = std::from_chars(text.data(), text.data() + text.size(), value);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
        return false;
    }
    outValue = value;
    return true;
}

bool TryParseDouble(std::string_view text, double& outValue) noexcept {
    if (text.empty()) {
        return false;
    }
    // Match std::from_chars<double>'s narrower grammar exactly (what the
    // strtod-based ParseDoubleInvariant below is a stand-in for): the
    // first character after an optional '-' (from_chars does not accept a
    // leading '+', matching TryParseInt64/UInt64 above) must be a digit or
    // '.', which rejects "inf"/"infinity"/"nan" (all start with a letter)
    // up front - strtod would otherwise happily accept those, along with
    // 0x-prefixed hex float literals, neither of which is part of this
    // project's one documented decimal grammar (see this file's own top
    // comment).
    std::size_t index = text.front() == '-' ? 1U : 0U;
    if (index >= text.size() || (text[index] != '.' && (text[index] < '0' || text[index] > '9'))) {
        return false;
    }
    if (text[index] == '0' && index + 1U < text.size() && (text[index + 1U] == 'x' || text[index + 1U] == 'X')) {
        return false;
    }

    const std::string buffer{ text };
    char* endPtr = nullptr;
    errno = 0;
    const double value = ParseDoubleInvariant(buffer.c_str(), &endPtr);
    if (endPtr != buffer.c_str() + buffer.size() || errno == ERANGE) {
        return false;
    }
    outValue = value;
    return true;
}

bool TryParseGuid(std::string_view text) noexcept {
    static constexpr std::size_t kGuidLength = 36U;
    static constexpr std::array<std::size_t, 4> kHyphenPositions{ 8U, 13U, 18U, 23U };
    if (text.size() != kGuidLength) {
        return false;
    }
    for (const std::size_t hyphenPosition : kHyphenPositions) {
        if (text[hyphenPosition] != '-') {
            return false;
        }
    }
    for (std::size_t i = 0U; i < kGuidLength; ++i) {
        const bool isHyphenPosition = text[i] == '-' && (i == 8U || i == 13U || i == 18U || i == 23U);
        if (isHyphenPosition) {
            continue;
        }
        if (!IsHexDigit(text[i])) {
            return false;
        }
    }
    return true;
}

bool TryParseColor(std::string_view text, kb::math::Color& outColor) noexcept {
    if (text.size() != 7U && text.size() != 9U) {
        return false;
    }
    if (text.front() != '#') {
        return false;
    }
    std::uint8_t red = 0U;
    std::uint8_t green = 0U;
    std::uint8_t blue = 0U;
    std::uint8_t alpha = 255U;
    if (!TryParseHexByte(text[1], text[2], red) || !TryParseHexByte(text[3], text[4], green) || !TryParseHexByte(text[5], text[6], blue)) {
        return false;
    }
    if (text.size() == 9U && !TryParseHexByte(text[7], text[8], alpha)) {
        return false;
    }
    outColor = kb::math::Color{
        .r = static_cast<float>(red) / 255.0F,
        .g = static_cast<float>(green) / 255.0F,
        .b = static_cast<float>(blue) / 255.0F,
        .a = static_cast<float>(alpha) / 255.0F,
    };
    return true;
}

bool TryParseDate(std::string_view text, std::chrono::year_month_day& outDate) noexcept {
    static constexpr std::size_t kDateLength = 10U;
    if (text.size() != kDateLength || text[4] != '-' || text[7] != '-') {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if (!TryParseDigits(text.substr(0U, 4U), year) || !TryParseDigits(text.substr(5U, 2U), month) || !TryParseDigits(text.substr(8U, 2U), day)) {
        return false;
    }
    const std::chrono::year_month_day candidate{
        std::chrono::year{ year },
        std::chrono::month{ static_cast<unsigned int>(month) },
        std::chrono::day{ static_cast<unsigned int>(day) },
    };
    if (!candidate.ok()) {
        return false;
    }
    outDate = candidate;
    return true;
}

} // namespace kb::library
