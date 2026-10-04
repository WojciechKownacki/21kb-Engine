#pragma once

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

// The fixed-step pose records are current, so the rows a transform sync composes are published to them.
[[nodiscard]] inline bool SceneFixedTransformPosesCurrent(const SceneState& state) noexcept {
    return state.fixedTransformTopologyVersion == state.hierarchyTopologyVersion &&
        state.fixedTransformRootAppendEpoch == state.hierarchyRootAppendEpoch;
}

// Forgets the transforms composed so far this frame, at the start of an Update.
void ResetSceneTransformRenderProxyUpdates(SceneState& state) noexcept;

// Builds the render-proxy lists (updated entities, their world affines, and the mesh, camera and light entries
// among them) from this frame's update bits, if a sync changed them since the last read.
void EnsureSceneTransformRenderProxyLists(const SceneState& state);

// Starts the scene's worker threads (shared by the transform passes and SceneRuntime::ParallelFor) if they are not running.
void EnsureSceneTransformWorkerPool(SceneState& state);

} // namespace kb::scene
