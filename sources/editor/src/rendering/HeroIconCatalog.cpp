#include "rendering/HeroIconCatalog.hpp"

#include "rendering/HeroIconAssets.hpp"

namespace kb::editor {

HeroIconGlyph HeroIconCatalog::Glyph(HeroIconKind icon) noexcept {
    switch (icon) {
    case HeroIconKind::Minus:
        return HeroIconAssets::Minus();
    case HeroIconKind::Play:
        return HeroIconAssets::Play();
    case HeroIconKind::Pause:
        return HeroIconAssets::Pause();
    case HeroIconKind::Resume:
        return HeroIconAssets::Resume();
    case HeroIconKind::Stop:
        return HeroIconAssets::Stop();
    case HeroIconKind::TransportStop:
        return HeroIconAssets::TransportStop();
    case HeroIconKind::XMark:
        return HeroIconAssets::XMark();
    case HeroIconKind::Cube:
        return HeroIconAssets::Cube();
    case HeroIconKind::Folder:
        return HeroIconAssets::Folder();
    case HeroIconKind::Eye:
        return HeroIconAssets::Eye();
    case HeroIconKind::MagnifyingGlass:
        return HeroIconAssets::MagnifyingGlass();
    case HeroIconKind::ChevronRight:
        return HeroIconAssets::ChevronRight();
    case HeroIconKind::ChevronDown:
        return HeroIconAssets::ChevronDown();
    case HeroIconKind::Plus:
        return HeroIconAssets::Plus();
    case HeroIconKind::EllipsisHorizontal:
        return HeroIconAssets::EllipsisHorizontal();
    case HeroIconKind::ListBullet:
        return HeroIconAssets::ListBullet();
    case HeroIconKind::AdjustmentsHorizontal:
        return HeroIconAssets::AdjustmentsHorizontal();
    case HeroIconKind::CommandLine:
        return HeroIconAssets::CommandLine();
    case HeroIconKind::Gamepad2:
        return HeroIconAssets::Gamepad2();
    default:
        return {};
    }
}

} // namespace kb::editor
