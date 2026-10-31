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
    [[nodiscard]] static HeroIconGlyph Check() noexcept;
    [[nodiscard]] static HeroIconGlyph Cube() noexcept;
    [[nodiscard]] static HeroIconGlyph Folder() noexcept;
    [[nodiscard]] static HeroIconGlyph Eye() noexcept;
    [[nodiscard]] static HeroIconGlyph MagnifyingGlass() noexcept;
    [[nodiscard]] static HeroIconGlyph ChevronRight() noexcept;
    [[nodiscard]] static HeroIconGlyph ChevronLeft() noexcept;
    [[nodiscard]] static HeroIconGlyph ChevronDown() noexcept;
    [[nodiscard]] static HeroIconGlyph SpeakerWave() noexcept;
    [[nodiscard]] static HeroIconGlyph Plus() noexcept;
    [[nodiscard]] static HeroIconGlyph EllipsisHorizontal() noexcept;
    [[nodiscard]] static HeroIconGlyph ListBullet() noexcept;
    [[nodiscard]] static HeroIconGlyph AdjustmentsHorizontal() noexcept;
    [[nodiscard]] static HeroIconGlyph CommandLine() noexcept;
    [[nodiscard]] static HeroIconGlyph DocumentText() noexcept;
    [[nodiscard]] static HeroIconGlyph Bolt() noexcept;
    [[nodiscard]] static HeroIconGlyph RectangleGroup() noexcept;
    [[nodiscard]] static HeroIconGlyph Gamepad2() noexcept;
    [[nodiscard]] static HeroIconGlyph RotationSnap() noexcept;
    [[nodiscard]] static HeroIconGlyph Camera() noexcept;
    [[nodiscard]] static HeroIconGlyph Skeleton() noexcept;
    [[nodiscard]] static HeroIconGlyph LockClosed() noexcept;
    [[nodiscard]] static HeroIconGlyph Server() noexcept;
    [[nodiscard]] static HeroIconGlyph WrenchScrewdriver() noexcept;
    [[nodiscard]] static HeroIconGlyph CodeBracket() noexcept;
    [[nodiscard]] static HeroIconGlyph RocketLaunch() noexcept;
    [[nodiscard]] static HeroIconGlyph Save() noexcept;
    [[nodiscard]] static HeroIconGlyph PlatformWindows() noexcept;
    [[nodiscard]] static HeroIconGlyph PlatformAndroid() noexcept;
    [[nodiscard]] static HeroIconGlyph PlatformLinux() noexcept;
    [[nodiscard]] static HeroIconGlyph PlatformServer() noexcept;
    [[nodiscard]] static HeroIconGlyph DisclosureCollapsed() noexcept;
    [[nodiscard]] static HeroIconGlyph DisclosureExpanded() noexcept;
    [[nodiscard]] static HeroIconGlyph Undo() noexcept;
    [[nodiscard]] static HeroIconGlyph Redo() noexcept;
    [[nodiscard]] static HeroIconGlyph Duplicate() noexcept;
    [[nodiscard]] static HeroIconGlyph Plug() noexcept;
    [[nodiscard]] static HeroIconGlyph Move() noexcept;
    [[nodiscard]] static HeroIconGlyph Rotate() noexcept;
    [[nodiscard]] static HeroIconGlyph Scale() noexcept;
    [[nodiscard]] static HeroIconGlyph Grid() noexcept;
    [[nodiscard]] static HeroIconGlyph Magnet() noexcept;
    [[nodiscard]] static HeroIconGlyph Map() noexcept;
    [[nodiscard]] static HeroIconGlyph Layers() noexcept;
    [[nodiscard]] static HeroIconGlyph Activity() noexcept;
    [[nodiscard]] static HeroIconGlyph Download() noexcept;
    [[nodiscard]] static HeroIconGlyph Upload() noexcept;
    [[nodiscard]] static HeroIconGlyph Navigation() noexcept;
    [[nodiscard]] static HeroIconGlyph BookOpen() noexcept;
    [[nodiscard]] static HeroIconGlyph Info() noexcept;
    [[nodiscard]] static HeroIconGlyph Flag() noexcept;
    [[nodiscard]] static HeroIconGlyph Monitor() noexcept;
};

} // namespace kb::editor
