#pragma once

#include "engine/ui/visual/UIText.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

// One laid-out line: glyphs [begin, end) of the broken text. `width` sums advance + spacing over those glyphs.
// Spaces at a wrap point belong to neither line; `hardBreak` marks a line ended by an authored '\n'.
struct UITextLineSpan {
    std::size_t begin = 0U;
    std::size_t end = 0U;
    float width = 0.0F;
    bool hardBreak = false;
};

// Ideographs, kana, hangul syllables and their full-width forms: text with no spaces between words, which
// may wrap between any two of these characters.
[[nodiscard]] bool IsUITextBreakableIdeographic(std::uint32_t codepoint) noexcept;
// Basic kinsoku shori: closing punctuation, small kana and iteration marks never start a line; opening
// brackets and quotes never end one.
[[nodiscard]] bool IsUITextLineStartProhibited(std::uint32_t codepoint) noexcept;
[[nodiscard]] bool IsUITextLineEndProhibited(std::uint32_t codepoint) noexcept;
// Whether a wrap may fall between `before` and `after` in the given wrap mode.
[[nodiscard]] bool UITextBreakAllowed(std::uint32_t before, std::uint32_t after, UITextWrapMode wrapMode) noexcept;

// Breaks text into lines no wider than `availableWidth`. Every '\n' ends a line in every wrap mode. Word mode
// wraps after spaces and between ideographic characters; a word wider than the line is split where it
// overflows. Character mode wraps between any two characters. Both keep the kinsoku rules wherever the line
// can still be broken elsewhere. `codepoints` and `advances` are parallel; '\n' has no advance.
void BreakUITextLines(std::span<const std::uint32_t> codepoints, std::span<const float> advances,
                      UITextWrapMode wrapMode, float availableWidth, float spacing,
                      std::vector<UITextLineSpan>& lines);

} // namespace kb::scene
