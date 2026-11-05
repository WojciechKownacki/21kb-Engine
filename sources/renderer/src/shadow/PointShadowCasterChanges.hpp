#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"
#include "shadow/PointShadowPassPlanner.hpp"

namespace kb::render {

// Keeps the cached tiles of local-light shadows honest: a light's tiles stay while nothing that casts a shadow
// has changed inside its range, bumped only by casters in the light's reach.
class PointShadowCasterChanges {
public:
    PointShadowCasterChanges() = delete;

    // Discards the cached tiles of every row of `setup` whose light a shadow caster change of the scene's current
    // motion frame reaches: a caster that moved, appeared, changed its mesh, material or skin, or started or
    // stopped casting, counted at both the place it left and the place it went to. Everything is discarded when
    // the cache missed a frame, when the render origin moved, and for a change no mesh proxy accounts for.
    // Runs once per motion frame; another viewport of the same frame changes nothing.
    static void Apply(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        const PointShadowSetup& setup,
        ShadowMapResource& map) noexcept;
};

} // namespace kb::render
