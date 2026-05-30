#include "rendering/HeroIconAssets.hpp"

#include <array>

namespace kb::editor {
namespace {

static constexpr std::array<HeroIconPath, 1> kMinus{
    HeroIconPath{ "M5 12h14", false },
};
static constexpr std::array<HeroIconPath, 1> kStop{
    HeroIconPath{ "M5.25 7.5A2.25 2.25 0 0 1 7.5 5.25h9a2.25 2.25 0 0 1 2.25 2.25v9a2.25 2.25 0 0 1-2.25 2.25h-9a2.25 2.25 0 0 1-2.25-2.25v-9Z", false },
};
static constexpr std::array<HeroIconPath, 1> kXMark{
    HeroIconPath{ "M6 18 18 6M6 6l12 12", false },
};
static constexpr std::array<HeroIconPath, 3> kCube{
    HeroIconPath{ "M12.3779 1.60217C12.1444 1.46594 11.8556 1.46594 11.6221 1.60217L3 6.63172L12 11.8817L21 6.63172L12.3779 1.60217Z", true },
    HeroIconPath{ "M21.75 7.93078L12.75 13.1808V22.1808L21.3779 17.1478C21.6083 17.0134 21.75 16.7668 21.75 16.5V7.93078Z", true },
    HeroIconPath{ "M11.25 22.1808V13.1808L2.25 7.93078V16.5C2.25 16.7668 2.39168 17.0134 2.6221 17.1478L11.25 22.1808Z", true },
};
static constexpr std::array<HeroIconPath, 2> kEye{
    HeroIconPath{ "M8 9.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Z", true },
    HeroIconPath{ "M1.38 8.28a.87.87 0 0 1 0-.566 7.003 7.003 0 0 1 13.244.005.87.87 0 0 1 0 .566A7.003 7.003 0 0 1 1.379 8.28ZM11 8a3 3 0 1 1-6 0 3 3 0 0 1 6 0Z", true },
};
static constexpr std::array<HeroIconPath, 1> kMagnifyingGlass{
    HeroIconPath{ "m21 21-5.197-5.197m0 0A7.5 7.5 0 1 0 5.196 5.196a7.5 7.5 0 0 0 10.607 10.607Z", false },
};
static constexpr std::array<HeroIconPath, 1> kChevronRight{
    HeroIconPath{ "m8.25 4.5 7.5 7.5-7.5 7.5", false },
};
static constexpr std::array<HeroIconPath, 1> kChevronDown{
    HeroIconPath{ "m19.5 8.25-7.5 7.5-7.5-7.5", false },
};
static constexpr std::array<HeroIconPath, 1> kPlus{
    HeroIconPath{ "M12 4.5v15m7.5-7.5h-15", false },
};
static constexpr std::array<HeroIconPath, 3> kEllipsisHorizontal{
    HeroIconPath{ "M6.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
    HeroIconPath{ "M12.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
    HeroIconPath{ "M18.75 12a.75.75 0 1 1-1.5 0 .75.75 0 0 1 1.5 0Z", true },
};

} // namespace

HeroIconGlyph HeroIconAssets::Minus() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kMinus } };
}

HeroIconGlyph HeroIconAssets::Stop() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kStop } };
}

HeroIconGlyph HeroIconAssets::XMark() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kXMark } };
}

HeroIconGlyph HeroIconAssets::Cube() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kCube } };
}

HeroIconGlyph HeroIconAssets::Eye() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kEye }, .viewBoxSize = 16.0F, .strokeWidth = 0.0F };
}

HeroIconGlyph HeroIconAssets::MagnifyingGlass() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kMagnifyingGlass } };
}

HeroIconGlyph HeroIconAssets::ChevronRight() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kChevronRight } };
}

HeroIconGlyph HeroIconAssets::ChevronDown() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kChevronDown } };
}

HeroIconGlyph HeroIconAssets::Plus() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kPlus } };
}

HeroIconGlyph HeroIconAssets::EllipsisHorizontal() noexcept {
    return HeroIconGlyph{ .paths = std::span<const HeroIconPath>{ kEllipsisHorizontal } };
}

} // namespace kb::editor
