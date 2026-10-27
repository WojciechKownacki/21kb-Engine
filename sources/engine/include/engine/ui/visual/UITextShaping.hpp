#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

// Glyphs are returned in visual order. Cluster bounds address UTF-32 codepoints in the
// original paragraph, including ligatures and combining sequences, rather than glyph indices.
struct UITextShapedGlyph {
    std::uint32_t glyphId = 0U;
    std::uint32_t clusterBegin = 0U;
    std::uint32_t clusterEnd = 0U;
    float advance = 0.0F;
    float offsetX = 0.0F;
    float offsetY = 0.0F; // Positive upwards, in font coordinates.
    bool rightToLeft = false;
};

// Shapes a logical line while resolving direction against the whole paragraph. The scale
// converts font design units to pixels, matching the rasterizer's ascent/descent scaling.
// Font bytes need only remain alive during the call. Failure clears output.
// Optional sorted style boundaries prohibit ligatures spanning differently styled text,
// while retaining surrounding context for cursive joining.
[[nodiscard]] bool ShapeUIText(std::span<const std::byte> fontBytes,
                               std::span<const std::uint32_t> paragraph,
                               std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                               std::vector<UITextShapedGlyph>& output,
                               std::span<const std::uint32_t> styleBoundaries = {});

} // namespace kb::scene
