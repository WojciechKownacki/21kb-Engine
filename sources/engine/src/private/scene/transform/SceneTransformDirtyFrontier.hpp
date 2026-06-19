#pragma once

#include "engine/ecs/Entity.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <limits>

namespace kb::scene {

inline void ClearSceneTransformDirtyFrontier(SceneState& state) noexcept {
    state.transformDirtyFrontierEntities.clear();
    if (state.transformDirtyFrontierMarkEpoch == std::numeric_limits<std::uint32_t>::max()) {
        state.transformDirtyFrontierMarkEpoch = 1U;
        std::ranges::fill(state.transformDirtyFrontierDenseMarkEpochs, 0U);
        state.transformDirtyFrontierSparseMarkEpochs.clear();
        return;
    }
    ++state.transformDirtyFrontierMarkEpoch;
}

[[nodiscard]] inline bool IsSceneTransformDirtyFrontierMarked(const SceneState& state, SceneEntity entity) noexcept {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        return denseIndex < state.transformDirtyFrontierDenseMarkEpochs.size()
            && state.transformDirtyFrontierDenseMarkEpochs[denseIndex] == state.transformDirtyFrontierMarkEpoch
            && denseIndex < state.transformDirtyFrontierDenseMarkedEntities.size()
            && state.transformDirtyFrontierDenseMarkedEntities[denseIndex] == entity;
    }

    const auto sparseMark = state.transformDirtyFrontierSparseMarkEpochs.find(entity.Id());
    return sparseMark != state.transformDirtyFrontierSparseMarkEpochs.end() && sparseMark->second == state.transformDirtyFrontierMarkEpoch;
}

inline void EnqueueSceneTransformDirtyFrontierUnchecked(SceneState& state, SceneEntity entity) {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t requiredSize = static_cast<std::size_t>(denseIndex) + 1U;
        if (state.transformDirtyFrontierDenseMarkEpochs.size() < requiredSize) {
            state.transformDirtyFrontierDenseMarkEpochs.resize(requiredSize, 0U);
            state.transformDirtyFrontierDenseMarkedEntities.resize(requiredSize);
        }
        if (IsSceneTransformDirtyFrontierMarked(state, entity)) {
            return;
        }
        state.transformDirtyFrontierDenseMarkEpochs[denseIndex] = state.transformDirtyFrontierMarkEpoch;
        state.transformDirtyFrontierDenseMarkedEntities[denseIndex] = entity;
        state.transformDirtyFrontierEntities.push_back(entity);
        return;
    }

    auto sparseMark = state.transformDirtyFrontierSparseMarkEpochs.find(entity.Id());
    if (sparseMark != state.transformDirtyFrontierSparseMarkEpochs.end() && sparseMark->second == state.transformDirtyFrontierMarkEpoch) {
        return;
    }
    if (sparseMark == state.transformDirtyFrontierSparseMarkEpochs.end()) {
        state.transformDirtyFrontierSparseMarkEpochs.emplace(entity.Id(), state.transformDirtyFrontierMarkEpoch);
    } else {
        sparseMark->second = state.transformDirtyFrontierMarkEpoch;
    }
    state.transformDirtyFrontierEntities.push_back(entity);
}

inline void EnqueueSceneTransformDirtyFrontier(SceneState& state, SceneEntity entity) noexcept {
    try {
        EnqueueSceneTransformDirtyFrontierUnchecked(state, entity);
    } catch (...) {
        ClearSceneTransformDirtyFrontier(state);
    }
}

} // namespace kb::scene
