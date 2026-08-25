#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

#include <cstddef>
#include <cstdint>

namespace kb::scene {

enum class SceneRenderProxyComponentMask : std::uint8_t {
    MeshRenderer = 1U << 0U,
    Camera = 1U << 1U,
    Light = 1U << 2U,
    Hidden = 1U << 3U,
};

[[nodiscard]] constexpr std::uint8_t SceneRenderProxyMask(SceneRenderProxyComponentMask mask) noexcept {
    return static_cast<std::uint8_t>(mask);
}

[[nodiscard]] inline std::uint8_t SceneRenderProxyComponentMaskOf(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.renderProxyDenseComponentMasks.size()) {
        return state.renderProxyDenseComponentMasks[denseIndex];
    }

    const auto mask = state.renderProxySparseComponentMasks.find(entity.Id());
    return mask == state.renderProxySparseComponentMasks.end() ? 0U : mask->second;
}

inline void SetSceneRenderProxyComponentMask(SceneState& state, SceneEntity entity, SceneRenderProxyComponentMask bit) {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t requiredSize = static_cast<std::size_t>(denseIndex) + 1U;
        if (state.renderProxyDenseComponentMasks.size() < requiredSize) {
            state.renderProxyDenseComponentMasks.resize(requiredSize);
        }
        state.renderProxyDenseComponentMasks[denseIndex] |= SceneRenderProxyMask(bit);
        return;
    }

    state.renderProxySparseComponentMasks[entity.Id()] |= SceneRenderProxyMask(bit);
}

inline void ClearSceneRenderProxyComponentMask(SceneState& state, SceneEntity entity, SceneRenderProxyComponentMask bit) noexcept {
    const std::uint8_t clearMask = static_cast<std::uint8_t>(~SceneRenderProxyMask(bit));
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.renderProxyDenseComponentMasks.size()) {
        state.renderProxyDenseComponentMasks[denseIndex] &= clearMask;
        return;
    }

    auto mask = state.renderProxySparseComponentMasks.find(entity.Id());
    if (mask == state.renderProxySparseComponentMasks.end()) {
        return;
    }
    mask->second &= clearMask;
    if (mask->second == 0U) {
        state.renderProxySparseComponentMasks.erase(mask);
    }
}

inline void ClearSceneRenderProxyComponentMask(SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.renderProxyDenseComponentMasks.size()) {
        state.renderProxyDenseComponentMasks[denseIndex] = 0U;
    }
    state.renderProxySparseComponentMasks.erase(entity.Id());
}

[[nodiscard]] constexpr bool SceneRenderProxyMaskHas(std::uint8_t mask, SceneRenderProxyComponentMask bit) noexcept {
    return (mask & SceneRenderProxyMask(bit)) != 0U;
}

inline void MarkSceneRenderProxyDirty(SceneState& state, SceneEntity entity) noexcept {
    ++state.renderProxyUpdateRevision;
    if (state.renderProxyUpdateRevision == 0U) {
        state.renderProxyUpdateRevision = 1U;
    }
    if (!state.renderProxyUpdateEntityIds.insert(entity.Id()).second) {
        return;
    }
    state.renderProxyUpdateEntities.push_back(entity);
}

inline void MarkSceneRenderProxySubtreeDirty(SceneState& state, SceneEntity root) noexcept {
    std::vector<SceneEntity>& traversal = state.renderProxyDirtyTraversalScratch;
    traversal.clear();
    traversal.push_back(root);
    for (std::size_t index = 0U; index < traversal.size(); ++index) {
        const SceneEntity entity = traversal[index];
        MarkSceneRenderProxyDirty(state, entity);
        const std::size_t childCount = SceneHierarchyCache::ChildCount(state, entity);
        for (std::size_t childIndex = 0U; childIndex < childCount; ++childIndex) {
            traversal.push_back(SceneHierarchyCache::ChildAt(state, entity, childIndex));
        }
    }
}

} // namespace kb::scene
