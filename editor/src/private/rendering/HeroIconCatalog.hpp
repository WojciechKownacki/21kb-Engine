#pragma once

#include "rendering/HeroIconKind.hpp"

#include <array>
#include <optional>
#include <span>

namespace kb::editor {

struct HeroIconLine {
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
};

struct HeroIconRoundedBox {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

struct HeroIconGlyph {
    std::span<const HeroIconLine> lines{};
    std::optional<HeroIconRoundedBox> roundedBox{};
};

class HeroIconCatalog {
public:
    HeroIconCatalog() = delete;

    [[nodiscard]] static HeroIconGlyph Glyph(HeroIconKind icon) noexcept {
        // Coordinates mirror the selected Heroicons 24-outline SVGs vendored in third_party/heroicons.
        static constexpr std::array<HeroIconLine, 1> minusLines{
            HeroIconLine{ 5.0F, 12.0F, 19.0F, 12.0F },
        };
        static constexpr std::array<HeroIconLine, 0> stopLines{};
        static constexpr std::array<HeroIconLine, 2> xMarkLines{
            HeroIconLine{ 6.0F, 18.0F, 18.0F, 6.0F },
            HeroIconLine{ 6.0F, 6.0F, 18.0F, 18.0F },
        };

        switch (icon) {
        case HeroIconKind::Minus:
            return HeroIconGlyph{ .lines = std::span<const HeroIconLine>{ minusLines } };
        case HeroIconKind::Stop:
            return HeroIconGlyph{
                .lines = std::span<const HeroIconLine>{ stopLines },
                .roundedBox = HeroIconRoundedBox{ 5.25F, 5.25F, 18.75F, 18.75F },
            };
        case HeroIconKind::XMark:
            return HeroIconGlyph{ .lines = std::span<const HeroIconLine>{ xMarkLines } };
        default:
            return {};
        }
    }
};

} // namespace kb::editor
