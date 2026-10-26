#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Versions and version ranges of mods: "major.minor.patch" (whole numbers, no leading zeros) and
// space-separated comparators that must all hold.
//
//     1.2.3            exactly 1.2.3
//     >=1.2.0 <2.0.0   1.2.0 up to, not including, 2.0.0
//     ^1.2.0           >=1.2.0 <2.0.0      (compatible with 1.2.0)
//     ~1.2.0           >=1.2.0 <1.3.0
//     *                any version
namespace kb::mods {

inline constexpr std::uint32_t kMaxModVersionNumber = 999999U;

struct ModVersion {
    std::uint32_t major = 0U;
    std::uint32_t minor = 0U;
    std::uint32_t patch = 0U;

    [[nodiscard]] auto operator<=>(const ModVersion&) const noexcept = default;
};

// The version of the mod interface this engine offers: the manifest format, the way a mod pack is
// mounted and the `Mods` script functions. A manifest names the range it was written for.
inline constexpr ModVersion kModApiVersion{ 1U, 0U, 0U };

[[nodiscard]] bool ParseModVersion(std::string_view text, ModVersion& out) noexcept;
[[nodiscard]] std::string ToString(const ModVersion& version);

struct ModVersionComparator {
    enum class Op : std::uint8_t { Equal, Greater, GreaterEqual, Less, LessEqual };
    Op op = Op::Equal;
    ModVersion version{};

    [[nodiscard]] bool operator==(const ModVersionComparator&) const noexcept = default;
};

struct ModVersionRange {
    // Every comparator must hold; none means any version.
    std::vector<ModVersionComparator> comparators;

    [[nodiscard]] bool operator==(const ModVersionRange&) const noexcept = default;
};

inline constexpr std::size_t kMaxModVersionComparators = 8U;

[[nodiscard]] bool ParseModVersionRange(std::string_view text, ModVersionRange& out);
[[nodiscard]] bool Satisfies(const ModVersionRange& range, const ModVersion& version) noexcept;
// The canonical text: "*" for any version, otherwise the comparators in order.
[[nodiscard]] std::string ToString(const ModVersionRange& range);

} // namespace kb::mods
