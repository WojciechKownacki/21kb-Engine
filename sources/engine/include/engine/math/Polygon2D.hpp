#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::math {
struct Polygon2DTriangulation {
    std::vector<std::array<float,2>> points;
    std::vector<std::uint32_t> indices;
};
// Simple outlines, no holes; input winding is arbitrary. The normalized output
// is clockwise in XY (counter-clockwise when Y is a screen-down coordinate).
[[nodiscard]] bool TriangulatePolygon2D(std::span<const std::array<float,2>> points,
    Polygon2DTriangulation& output,std::string& error);
}
