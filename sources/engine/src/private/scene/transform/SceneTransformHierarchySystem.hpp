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

} // namespace kb::scene
