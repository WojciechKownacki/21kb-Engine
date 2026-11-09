#pragma once
#include "kb/render/scene/SceneRenderTypes.hpp"

namespace kb::render {
[[nodiscard]] inline SceneRenderIblConfig ResolveSceneIblConfig(
    const SceneRenderIblConfig& requested, const SceneRenderIblConfig& fallback) noexcept {
    constexpr SceneRenderIblConfig defaults{};
    auto result = requested.HasEnvironment() || requested.reflectionProbeCount != 0U ? requested : fallback;
    // Flags also apply to probes authored by the scene, resolved after this merge.
    if (requested.localProbeBlending != defaults.localProbeBlending) result.localProbeBlending = requested.localProbeBlending;
    if (requested.parallaxCorrection != defaults.parallaxCorrection) result.parallaxCorrection = requested.parallaxCorrection;
    return result;
}
}
