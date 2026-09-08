#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kb::render {

struct ScreenUITextMarkupGlyph {
    std::uint32_t codepoint = 0U;
    std::array<float, 4U> color{1.0F, 1.0F, 1.0F, 1.0F};
    // Byte offset of this glyph in the authored string. Markup tags are consumed, so glyph
    // index and byte offset drift apart; a text caret is addressed in bytes and needs this to
    // find its column.
    std::uint32_t sourceOffset = 0U;
};

struct ScreenUITextMarkup {
    std::vector<ScreenUITextMarkupGlyph> glyphs;
    bool succeeded = true;
};

class ScreenUITextMarkupParser {
  public:
    [[nodiscard]] ScreenUITextMarkup Parse(std::string_view text, bool enabled) const;
};

} // namespace kb::render
