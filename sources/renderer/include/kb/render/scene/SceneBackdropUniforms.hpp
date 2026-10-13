#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>

namespace kb::render {

class RenderScene;

// The u_deferredBackdrop* uniforms of sky_backdrop.sh: what the deferred lighting draws behind the
// scene and what the sky light captures for reflections, from one source.
struct SceneBackdropUniformValues {
    // x: 1 vertical gradient, 2 equirectangular environment map, 3 procedural sky (0 none).
    std::array<float, 4> horizon{};
    std::array<float, 4> zenith{};
    std::array<float, 4> params{};
    // Toward the brightest directional light (w = 1 when the scene has one) and its colour * intensity.
    std::array<float, 4> sun{ 0.0F, 1.0F, 0.0F, 0.0F };
    std::array<float, 4> sunColor{};

    [[nodiscard]] bool HasSky() const noexcept { return params[0] > 0.5F; }
    [[nodiscard]] bool operator==(const SceneBackdropUniformValues&) const noexcept = default;
};

// environmentAvailable: the equirectangular map of an EnvironmentMap backdrop is loaded.
[[nodiscard]] SceneBackdropUniformValues BuildSceneBackdropUniforms(
    const SceneRenderWorldBackdrop* backdrop, bool environmentAvailable, const RenderScene& renderScene) noexcept;

} // namespace kb::render
