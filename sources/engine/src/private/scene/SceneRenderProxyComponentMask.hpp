#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "scene/SceneState.hpp"

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

} // namespace kb::scene
