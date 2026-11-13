#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"
#include "shadow/PointShadowPassPlanner.hpp"

namespace kb::render {

// Keeps the cached shadow of a map honest: a light's tiles or a cascade stay while nothing that casts a shadow has
// changed inside their reach. A change is a caster that moved, appeared, changed its mesh, material or skin, or
// started or stopped casting, counted at both the place it left and the place it went to. Everything is
// discarded when the cache missed a frame, when the render origin moved, and for a change no mesh proxy accounts for.
// Each call runs once per motion frame; a second view in that frame changes nothing.
class ShadowCasterChanges {
public:
    ShadowCasterChanges() = delete;

    // Discards the cached tiles of every row of `setup` whose light a change reaches: the casters within its range.
    static void ApplyToLocalLights(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        const PointShadowSetup& setup,
        ShadowMapResource& map) noexcept;

    // Discards every cached cascade of `map` whose window (ShadowMapResource::CachedCascade) a change reaches: the
    // casters over the window, at any depth.
    static void ApplyToCascades(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        ShadowMapResource& map) noexcept;
};

} // namespace kb::render
