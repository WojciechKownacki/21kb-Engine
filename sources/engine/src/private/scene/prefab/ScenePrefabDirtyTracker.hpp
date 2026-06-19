#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "scene/SceneState.hpp"

#include <cstdint>

namespace kb::scene {

inline void MarkScenePrefabNodeDirty(SceneState& state, SceneEntity entity) {
    if (state.suppressPrefabDirtyTracking) {
        return;
    }

    std::uint32_t nodeIndex = 0U;
    const ScenePrefabInstanceHandle handle = state.prefabInstances.FindContainingEntity(entity, nodeIndex);
    if (handle.IsValid()) {
        state.prefabInstances.MarkNodeDirty(handle, nodeIndex);
    }
}

inline void MarkScenePrefabTopologyDirty(SceneState& state, SceneEntity entity) {
    if (state.suppressPrefabDirtyTracking) {
        return;
    }

    std::uint32_t nodeIndex = 0U;
    const ScenePrefabInstanceHandle handle = state.prefabInstances.FindContainingEntity(entity, nodeIndex);
    if (handle.IsValid()) {
        state.prefabInstances.MarkNodeDirty(handle, nodeIndex);
        state.prefabInstances.MarkTopologyDirty(handle);
    }
}

} // namespace kb::scene
