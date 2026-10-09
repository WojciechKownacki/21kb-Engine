#pragma once

#include "engine/math/DVec3.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

namespace kb::render {

// Particle snapshots and GPU emitter commands hold positions relative to the particle simulation origin
// (ParticlePlayback::SimulationOrigin); the renderer draws relative to its render origin (docs/large_worlds.md).
// The renderer keeps the simulation origin on its render origin, so the offset between the two, which is added to
// particle positions, is zero except in the frames after the render origin moved and before the simulation
// followed it.
[[nodiscard]] inline kb::math::Vec3 ParticleRenderOffset(
    const kb::math::DVec3& simulationOrigin, const kb::math::DVec3& renderOrigin) noexcept {
    return kb::math::RelativeTo(simulationOrigin, renderOrigin);
}

// `camera` seen from particle space: view depths and camera positions taken with it for particle positions are
// those of the positions plus `offset` taken with `camera`.
[[nodiscard]] inline SceneRenderCamera ParticleSpaceCamera(const SceneRenderCamera& camera, kb::math::Vec3 offset) noexcept {
    SceneRenderCamera result = camera;
    for (std::size_t row = 0U; row < 3U; ++row) {
        result.view[12U + row] += camera.view[row] * offset.x + camera.view[4U + row] * offset.y + camera.view[8U + row] * offset.z;
    }
    return result;
}

} // namespace kb::render
