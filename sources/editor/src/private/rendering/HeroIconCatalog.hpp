#pragma once

#include "rendering/HeroIconKind.hpp"

#include <span>
#include <string_view>

namespace kb::editor {

struct HeroIconPath {
    std::string_view data{};
    bool filled = false;
};

struct HeroIconGlyph {
    std::span<const HeroIconPath> paths{};
    float viewBoxSize = 24.0F;
    float strokeWidth = 1.5F;
};

class HeroIconCatalog {
public:
    HeroIconCatalog() = delete;

    [[nodiscard]] static HeroIconGlyph Glyph(HeroIconKind icon) noexcept;
};

} // namespace kb::editor
