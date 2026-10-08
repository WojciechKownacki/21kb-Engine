#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace kb::localization {

// CLDR plural categories, in CLDR order.
enum class PluralCategory : std::uint8_t {
    Zero,
    One,
    Two,
    Few,
    Many,
    Other,
};

// The CLDR plural operands of one number:
//   n  absolute value (integer part `i` plus fraction `f` / 10^v)
//   i  integer digits of n
//   v  number of visible fraction digits, with trailing zeros
//   w  number of visible fraction digits, without trailing zeros
//   f  visible fraction digits, with trailing zeros, as an integer
//   t  visible fraction digits, without trailing zeros, as an integer
//   e  compact decimal exponent (the `c` operand is the same value)
// "1.50" has i=1 v=2 w=1 f=50 t=5; "1.2c6" (1 200 000 in compact form) has i=1200000 e=6.
struct PluralOperands {
    std::uint64_t i = 0U;
    std::uint64_t f = 0U;
    std::uint64_t t = 0U;
    std::uint32_t v = 0U;
    std::uint32_t w = 0U;
    std::uint32_t e = 0U;

    [[nodiscard]] bool operator==(const PluralOperands&) const noexcept = default;
};

[[nodiscard]] std::string_view PluralCategoryName(PluralCategory category) noexcept;
[[nodiscard]] std::optional<PluralCategory> ParsePluralCategoryName(std::string_view name) noexcept;

[[nodiscard]] PluralOperands PluralOperandsFromInteger(std::int64_t value) noexcept;
// Parses a plain decimal ("-12", "1.50") optionally followed by a compact exponent ("1.2c6", "3e3").
// Visible trailing fraction zeros are significant, as CLDR requires. Returns nothing for malformed text or
// for values with more than 18 integer or 18 fraction digits.
[[nodiscard]] std::optional<PluralOperands> ParsePluralOperands(std::string_view number) noexcept;

// Selects the plural category of a number in a language. `language` is a BCP 47 tag or locale id
// ("pl", "pt-PT", "zh_Hant"); matching is case-insensitive and falls back from the full tag to its
// language subtag. Languages without rules use `Other` for every number.
[[nodiscard]] PluralCategory SelectPluralCategory(std::string_view language, const PluralOperands& operands) noexcept;
[[nodiscard]] PluralCategory SelectPluralCategory(std::string_view language, std::int64_t value) noexcept;

// Whether a language has its own rules (instead of the `Other`-only default).
[[nodiscard]] bool HasPluralRules(std::string_view language) noexcept;
// The categories a language can select, in CLDR order; always ends with `Other`.
[[nodiscard]] std::span<const PluralCategory> PluralCategoriesOf(std::string_view language) noexcept;

} // namespace kb::localization
