#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Grapheme clusters of UTF-8 text: what a player sees as one character, and so what one Backspace erases, one
// arrow key steps over and one unit of a character limit counts. A cluster is a base character with its
// combining marks, variation selectors, emoji modifiers and joiners; an emoji joined by U+200D to the next
// pictograph; a pair of regional indicators (a flag); a Hangul syllable written as its jamo; or a CR LF pair.
// This follows the rules of Unicode Standard Annex 29 for these cases (not the full tables of every script):
// text outside them breaks between code points. Invalid UTF-8 is read as single bytes and never crashes.
//
// Offsets are byte offsets into `utf8`. An offset inside a cluster (or inside a code point) is treated as
// belonging to that cluster.

// Marks that never start a cluster: combining marks, variation selectors, joiners, emoji modifiers, tags.
[[nodiscard]] bool IsUITextGraphemeExtender(std::uint32_t codepoint) noexcept;

// The end of the cluster that contains `offset` (the next boundary after it); utf8.size() at the end.
[[nodiscard]] std::size_t NextUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
// The start of the cluster before `offset`: the greatest boundary strictly below it; 0 at the start.
[[nodiscard]] std::size_t PreviousUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
// `offset` moved back to the start of the cluster that contains it.
[[nodiscard]] std::size_t SnapToUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept;
[[nodiscard]] std::size_t UITextGraphemeCount(std::string_view utf8) noexcept;

} // namespace kb::scene
