#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Unicode 17.0.0 extended grapheme clusters (UAX #29), used by editing, selection and character limits.
// Invalid UTF-8 bytes each form an independent cluster.
//
// Offsets are byte offsets into `utf8`. An offset inside a cluster (or inside a code point) is treated as
// belonging to that cluster.

// Extend, ZWJ and SpacingMark properties. At the start of text these can form their own cluster.
[[nodiscard]] bool IsUITextGraphemeExtender(std::uint32_t codepoint) noexcept;

// The end of the cluster that contains `offset` (the next boundary after it); utf8.size() at the end.
[[nodiscard]] std::size_t NextUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
// The start of the cluster before `offset`: the greatest boundary strictly below it; 0 at the start.
[[nodiscard]] std::size_t PreviousUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
// `offset` moved back to the start of the cluster that contains it.
[[nodiscard]] std::size_t SnapToUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
[[nodiscard]] std::size_t UITextGraphemeCount(std::string_view utf8) noexcept;

} // namespace kb::scene
