#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
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
    // Font-provided GDEF ligature caret positions relative to the glyph origin,
    // in pixels. Empty when the font does not provide them.
    std::vector<float> ligatureCarets;
};

// OpenType axis tag in big-endian byte order (for example 0x77676874 for "wght").
struct UITextFontVariation {
    std::uint32_t tag = 0U;
    float value = 0.0F;
};

// Parse comma-separated four-byte OpenType tags and decimal values, e.g. "wght=450,wdth=90".
// Empty text selects the font defaults. On failure output is cleared.
[[nodiscard]] bool ParseUITextFontVariations(std::string_view text, std::vector<UITextFontVariation>& output);

// Shapes a logical line while resolving direction against the whole paragraph. The scale
// converts font design units to pixels, matching the rasterizer's ascent/descent scaling.
// Font bytes need only remain alive during the call. Failure clears output.
// Optional sorted style boundaries prohibit ligatures spanning differently styled text,
// while retaining surrounding context for cursive joining.
// Variation axes must exist in the face, have finite in-range values and appear at most once.
[[nodiscard]] bool ShapeUIText(std::span<const std::byte> fontBytes,
                               std::span<const std::uint32_t> paragraph,
                               std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                               std::vector<UITextShapedGlyph>& output,
                               std::span<const std::uint32_t> styleBoundaries = {});
[[nodiscard]] bool ShapeUIText(std::span<const std::byte> fontBytes,
                               std::span<const std::uint32_t> paragraph,
                               std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                               std::vector<UITextShapedGlyph>& output,
                               std::span<const std::uint32_t> styleBoundaries,
                               std::span<const UITextFontVariation> variations);

// The original glyph type remains unchanged; each result records which font owns it.
struct UITextFontShapedGlyph {
    UITextShapedGlyph glyph;
    bool fallback = false;
};

// Grapheme boundaries are strictly increasing UTF-32 paragraph indices, include both
// line endpoints and any paragraph boundaries within the line. The caller supplies
// Unicode grapheme segmentation. Fallback uses its own pixel scale and default variation
// coordinates. When neither face can shape a grapheme, primary is used. Coverage is
// probed per grapheme; contextual substitutions can still produce missing glyphs.
[[nodiscard]] bool ShapeUITextWithFallback(std::span<const std::byte> fontBytes,
    std::span<const std::byte> fallbackBytes, std::span<const std::uint32_t> paragraph,
    std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale, float fallbackUnitScale,
    std::span<const std::uint32_t> graphemeBoundaries, std::vector<UITextFontShapedGlyph>& output,
    std::span<const std::uint32_t> styleBoundaries = {},
    std::span<const UITextFontVariation> variations = {}, std::string_view language = {});

} // namespace kb::scene
