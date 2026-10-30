#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

// Produces an alpha-channel signed distance field with an eight-pixel border.
// The contour is encoded at 128 and each pixel of distance spans 16 levels.
// Empty glyphs are represented by an empty output. Failure, including a thin
// glyph without an interior sample, requires the caller to use a bitmap.
[[nodiscard]] bool BuildFontDistanceField(std::span<const std::uint8_t> coverage, int width, int height,
                                          std::vector<std::uint8_t>& output);

inline constexpr int kFontDistanceFieldPadding = 8;
inline constexpr float kFontDistanceFieldScale = 16.0F;

} // namespace kb::render
