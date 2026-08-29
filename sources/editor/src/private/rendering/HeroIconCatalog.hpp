#pragma once

#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <span>
#include <string_view>

namespace kb::editor {

struct HeroIconPath {
    std::string_view data{};
    bool filled = false;
    // How a filled path decides what is inside it. The vendored solid Heroicons declare
    // fill-rule="evenodd", so that stays the default; a glyph whose subpaths are meant to
    // stack rather than punch holes in each other sets this false.
    bool evenOdd = true;
    // A brand mark carries its own colours, so a path may name one. CLR_INVALID keeps the
    // default: the glyph takes whatever colour the caller is drawing in.
    COLORREF color = CLR_INVALID;
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
