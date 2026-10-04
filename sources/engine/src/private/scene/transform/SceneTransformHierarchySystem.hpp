#pragma once

#include "engine/scene/SceneTransforms.hpp"

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

class SceneComponentRegistry;

// LIB-089: this is the actual recompute pass behind
// kb::scene::SceneRuntime::SynchronizeTransforms() — see that method's own
// doc comment (engine/scene/SceneRuntime.hpp) for the full per-consumer
// (scripts/physics/renderer) timing contract for WHEN world* is fresh.
// This class itself is purely mechanical: given the current local* values
// and hierarchy topology in `state`, recompute every dirty entity's
// world* fields — it has no opinion on scheduling, that lives entirely in
// SceneRuntimeService::Update's call sites (SceneRuntime.cpp).
class SceneTransformHierarchySystem {
public:
    void Update(SceneState& state) const;
};

// Forgets the transforms composed in the last frame, once: by the first transform pass after an Update or by the
// next Update.
void BeginSceneTransformRenderProxyUpdates(SceneState& state) noexcept;

// Transforms().ParallelForEachRoot.
TransformPassStats RunSceneTransformPass(SceneState& state, std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents,
    SceneTransforms::TransformRangeBody body, void* context);

// The sizes of the render-proxy lists, counted from this frame's update bits and the mesh, camera and light
// components without building the lists.
struct SceneTransformRenderProxyCounts {
    std::size_t updated = 0U;
    std::size_t meshRenderers = 0U;
    std::size_t visibleMeshRenderers = 0U;
    std::size_t cameras = 0U;
    std::size_t lights = 0U;
    std::size_t identityAffine = 0U;
};
[[nodiscard]] SceneTransformRenderProxyCounts CountSceneTransformRenderProxyUpdates(const SceneState& state);

// Builds the render-proxy lists (updated entities, their world affines, and the mesh, camera and light entries
// among them) from this frame's update bits, if a sync changed them since the last read.
void EnsureSceneTransformRenderProxyLists(const SceneState& state);

// Starts the scene's worker threads (shared by the transform passes and SceneRuntime::ParallelFor) if they are not running.
void EnsureSceneTransformWorkerPool(SceneState& state);

} // namespace kb::scene
