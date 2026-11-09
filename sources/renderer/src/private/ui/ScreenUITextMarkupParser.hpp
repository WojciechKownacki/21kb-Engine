#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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
    // One-based index into ScreenUITextMarkup::links; zero means ordinary text.
    std::uint32_t link = 0U;
    bool underline = false;
};

struct ScreenUITextMarkupRange {
    std::size_t begin = 0U;
    std::size_t end = 0U;
};
struct ScreenUITextMarkupRow {
    std::vector<ScreenUITextMarkupRange> cells;
};
struct ScreenUITextMarkupBlock {
    bool table = false;
    ScreenUITextMarkupRange text;
    std::vector<ScreenUITextMarkupRow> rows;
};

struct ScreenUITextMarkup {
    std::vector<ScreenUITextMarkupGlyph> glyphs;
    std::vector<std::string> links;
    std::vector<ScreenUITextMarkupBlock> blocks;
    bool succeeded = true;
};

class ScreenUITextMarkupParser {
  public:
    [[nodiscard]] ScreenUITextMarkup Parse(std::string_view text, bool enabled, bool parseLinks = false,
                                           bool parseTables = false) const;
};

} // namespace kb::render
