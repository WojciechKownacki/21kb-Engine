#pragma once

#include "rendering/HeroIconCatalog.hpp"

namespace kb::editor {

class HeroIconAssets {
public:
    HeroIconAssets() = delete;

    [[nodiscard]] static HeroIconGlyph Minus() noexcept;
    [[nodiscard]] static HeroIconGlyph Play() noexcept;
    [[nodiscard]] static HeroIconGlyph Pause() noexcept;
    [[nodiscard]] static HeroIconGlyph Resume() noexcept;
    [[nodiscard]] static HeroIconGlyph Stop() noexcept;
    [[nodiscard]] static HeroIconGlyph TransportStop() noexcept;
    [[nodiscard]] static HeroIconGlyph XMark() noexcept;
    [[nodiscard]] static HeroIconGlyph Cube() noexcept;
    [[nodiscard]] static HeroIconGlyph Folder() noexcept;
    [[nodiscard]] static HeroIconGlyph Eye() noexcept;
    [[nodiscard]] static HeroIconGlyph MagnifyingGlass() noexcept;
    [[nodiscard]] static HeroIconGlyph ChevronRight() noexcept;
    [[nodiscard]] static HeroIconGlyph ChevronDown() noexcept;
    [[nodiscard]] static HeroIconGlyph Plus() noexcept;
    [[nodiscard]] static HeroIconGlyph EllipsisHorizontal() noexcept;
    [[nodiscard]] static HeroIconGlyph ListBullet() noexcept;
    [[nodiscard]] static HeroIconGlyph AdjustmentsHorizontal() noexcept;
    [[nodiscard]] static HeroIconGlyph CommandLine() noexcept;
    [[nodiscard]] static HeroIconGlyph Gamepad2() noexcept;
    [[nodiscard]] static HeroIconGlyph RotationSnap() noexcept;
};

} // namespace kb::editor
