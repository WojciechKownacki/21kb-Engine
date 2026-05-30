#include "rendering/HeroIconCatalog.hpp"

#include "rendering/HeroIconAssets.hpp"

namespace kb::editor {

HeroIconGlyph HeroIconCatalog::Glyph(HeroIconKind icon) noexcept {
    switch (icon) {
    case HeroIconKind::Minus:
        return HeroIconAssets::Minus();
    case HeroIconKind::Stop:
        return HeroIconAssets::Stop();
    case HeroIconKind::XMark:
        return HeroIconAssets::XMark();
    case HeroIconKind::Cube:
        return HeroIconAssets::Cube();
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
    default:
        return {};
    }
}

} // namespace kb::editor
