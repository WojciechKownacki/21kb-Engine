#pragma once

#include <array>

namespace kb::render {

// A flat, unlit, translucent triangle drawn in the editor overlay pass before its lines (filled debug
// surfaces such as the walkable area of a navigation mesh). Positions are world space like
// PhysicsDebugLine; the renderer moves them relative to the render origin.
struct DebugFillTriangle {
    std::array<std::array<float, 3>, 3> corners{};
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
};

} // namespace kb::render
