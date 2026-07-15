#pragma once

#include <array>

namespace kb::render {

// LIB-132: a single 3D line segment for the physics debug-draw overlay (collider/character/
// joint wireframes, single-query trace). Deliberately a flat, renderer-owned type - kb::scene
// (kb_engine) has no bgfx/renderer dependency, so the editor/game layer that DOES depend on
// both converts kb::scene::PhysicsDebugLineDesc into this type each frame.
struct PhysicsDebugLine {
    std::array<float, 3> from{0.0F, 0.0F, 0.0F};
    std::array<float, 3> to{0.0F, 0.0F, 0.0F};
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
};

} // namespace kb::render
