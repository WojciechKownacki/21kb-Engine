#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kb::render {

struct ScreenUITextMarkupGlyph {
    std::uint32_t codepoint = 0U;
    std::array<float, 4U> color{1.0F, 1.0F, 1.0F, 1.0F};
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
