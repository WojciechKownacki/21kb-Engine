#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace kb::scene {

class SceneHierarchyCache {
public:
    SceneHierarchyCache() = delete;

    static void Add(SceneState& state, SceneEntity entity, SceneEntity parent) {
        state.hierarchyParents[entity.Id()] = parent;
        SetDenseParent(state, entity, parent);
        AddToParentList(state, parent, entity);
        MarkTopologyDirty(state);
    }

    static void AddRoot(SceneState& state, SceneEntity entity) {
        Add(state, entity, {});
    }

    static void AssignOrder(SceneState& state, SceneEntity entity) {
        SetOrder(state, entity, state.nextHierarchyOrder++);
    }

    [[nodiscard]] static SceneEntity Parent(const SceneState& state, SceneEntity entity) noexcept {
        if (const SceneEntity* parent = DenseParent(state, entity); parent != nullptr) {
            return *parent;
        }
        const auto parent = state.hierarchyParents.find(entity.Id());
        return parent == state.hierarchyParents.end() ? SceneEntity{} : parent->second;
    }

    static void AddMany(SceneState& state, std::span<const SceneEntity> entities, std::span<const SceneEntity> parents) {
        if (entities.size() != parents.size()) {
            throw std::invalid_argument("Scene hierarchy cache bulk add requires matching entity and parent counts");
        }

        state.hierarchyParents.reserve(state.hierarchyParents.size() + entities.size());
        state.hierarchyChildren.reserve(state.hierarchyChildren.size() + entities.size());
        state.hierarchyRoots.reserve(state.hierarchyRoots.size() + entities.size());
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const SceneEntity entity = entities[index];
            const SceneEntity parent = parents[index];
            state.hierarchyParents[entity.Id()] = parent;
            SetDenseParent(state, entity, parent);
            if (parent.IsValid()) {
                state.hierarchyChildren[parent.Id()].push_back(entity);
                AddDenseChild(state, parent, entity);
            } else {
                state.hierarchyRoots.push_back(entity);
            }
        }
        MarkTopologyDirty(state);
    }

    static void AddManyDense(SceneState& state, std::span<const SceneEntity> entities, std::span<const SceneEntity> parents) {
        if (entities.size() != parents.size()) {
            throw std::invalid_argument("Scene hierarchy cache dense bulk add requires matching entity and parent counts");
        }
        if (entities.empty()) {
            return;
        }

        std::uint32_t maxEntityIndex = 0;
        std::uint32_t maxParentIndex = 0;
        bool hasDenseEntity = false;
        bool hasDenseParent = false;
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const std::uint32_t entityIndex = DenseIndex(entities[index]);
            if (entityIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                maxEntityIndex = hasDenseEntity ? std::max(maxEntityIndex, entityIndex) : entityIndex;
                hasDenseEntity = true;
            }
            const std::uint32_t parentIndex = DenseIndex(parents[index]);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                maxParentIndex = hasDenseParent ? std::max(maxParentIndex, parentIndex) : parentIndex;
                hasDenseParent = true;
            }
        }

        if (hasDenseEntity && state.denseHierarchyParents.size() <= maxEntityIndex) {
            state.denseHierarchyParents.resize(static_cast<std::size_t>(maxEntityIndex) + 1U);
        }
        if (hasDenseParent && state.denseHierarchyChildren.size() <= maxParentIndex) {
            state.denseHierarchyChildren.resize(static_cast<std::size_t>(maxParentIndex) + 1U);
        }

        std::vector<std::size_t> childCounts(hasDenseParent ? static_cast<std::size_t>(maxParentIndex) + 1U : 0U, 0U);
        std::size_t fallbackParentCount = 0;
        std::size_t rootCount = 0;
        for (SceneEntity parent : parents) {
            if (!parent.IsValid()) {
                ++rootCount;
                continue;
            }
            const std::uint32_t parentIndex = DenseIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                ++childCounts[parentIndex];
            } else {
                ++fallbackParentCount;
            }
        }

        state.hierarchyRoots.reserve(state.hierarchyRoots.size() + rootCount);
        for (std::uint32_t index = 0; index < childCounts.size(); ++index) {
            if (childCounts[index] != 0U) {
                state.denseHierarchyChildren[index].reserve(state.denseHierarchyChildren[index].size() + childCounts[index]);
            }
        }
        if (fallbackParentCount != 0U) {
            state.hierarchyChildren.reserve(state.hierarchyChildren.size() + fallbackParentCount);
        }

        AssignDenseOrderRange(state, entities);
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const SceneEntity entity = entities[index];
            const SceneEntity parent = parents[index];
            const std::uint32_t entityIndex = DenseIndex(entity);
            if (entityIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyParents[entityIndex] = parent;
            } else {
                state.hierarchyParents.emplace(entity.Id(), parent);
            }

            if (!parent.IsValid()) {
                state.hierarchyRoots.push_back(entity);
                continue;
            }

            const std::uint32_t parentIndex = DenseIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyChildren[parentIndex].push_back(entity);
            } else {
                state.hierarchyChildren[parent.Id()].push_back(entity);
            }
        }
        MarkTopologyDirty(state);
    }

    static void AssignOrderRange(SceneState& state, std::span<const SceneEntity> entities) {
        AssignDenseOrderRange(state, entities);
    }

    static void AssignDenseOrderRange(SceneState& state, std::span<const SceneEntity> entities) {
        const std::uint64_t firstOrder = state.nextHierarchyOrder;
        state.nextHierarchyOrder += entities.size();
        std::size_t sparseCount = 0U;
        for (SceneEntity entity : entities) {
            if (DenseIndex(entity) == kb::ecs::kInvalidGeneratedEntityIndex) {
                ++sparseCount;
            }
        }
        if (sparseCount != 0U) {
            state.hierarchyOrder.reserve(state.hierarchyOrder.size() + sparseCount);
        }
        for (std::size_t index = 0; index < entities.size(); ++index) {
            SetOrder(state, entities[index], firstOrder + index);
        }
    }

    static void Move(SceneState& state, SceneEntity child, SceneEntity oldParent, SceneEntity newParent) {
        RemoveFromParentList(state, oldParent, child);
        state.hierarchyParents[child.Id()] = newParent;
        SetDenseParent(state, child, newParent);
        AddToParentList(state, newParent, child);
        MarkTopologyDirty(state);
    }

    static void Remove(SceneState& state, SceneEntity entity, SceneEntity parent) {
        RemoveFromParentList(state, parent, entity);
        state.hierarchyParents.erase(entity.Id());
        state.hierarchyChildren.erase(entity.Id());
        state.hierarchyOrder.erase(entity.Id());
        ClearDenseEntry(state, entity);
        MarkTopologyDirty(state);
    }

    [[nodiscard]] static std::vector<SceneEntity> Roots(const SceneState& state) {
        return state.hierarchyRoots;
    }

    [[nodiscard]] static std::vector<SceneEntity> Children(const SceneState& state, SceneEntity entity) {
        if (const std::vector<SceneEntity>* children = DenseChildren(state, entity); children != nullptr) {
            return *children;
        }
        const auto children = state.hierarchyChildren.find(entity.Id());
        return children == state.hierarchyChildren.end() ? std::vector<SceneEntity>{} : children->second;
    }

    // LIB-087: unlike Children() above, these never copy the child vector —
    // both index directly into whichever storage (dense array or sparse
    // fallback map) already holds it, the same two-tier lookup Children()
    // itself already does, just without materializing a new vector for a
    // single count or a single element.
    [[nodiscard]] static std::size_t ChildCount(const SceneState& state, SceneEntity entity) noexcept {
        if (const std::vector<SceneEntity>* children = DenseChildren(state, entity); children != nullptr) {
            return children->size();
        }
        const auto children = state.hierarchyChildren.find(entity.Id());
        return children == state.hierarchyChildren.end() ? 0U : children->second.size();
    }

    [[nodiscard]] static SceneEntity ChildAt(const SceneState& state, SceneEntity entity, std::size_t index) noexcept {
        if (const std::vector<SceneEntity>* children = DenseChildren(state, entity); children != nullptr) {
            return index < children->size() ? (*children)[index] : SceneEntity{};
        }
        const auto children = state.hierarchyChildren.find(entity.Id());
        if (children == state.hierarchyChildren.end() || index >= children->second.size()) {
            return SceneEntity{};
        }
        return children->second[index];
    }

private:
    static void MarkTopologyDirty(SceneState& state) noexcept {
        ++state.hierarchyTopologyVersion;
        if (state.hierarchyTopologyVersion == 0U) {
            state.hierarchyTopologyVersion = 1U;
        }
    }

    [[nodiscard]] static std::uint32_t DenseIndex(SceneEntity entity) noexcept {
        return kb::ecs::GeneratedEntityIndex(entity);
    }

    static void EnsureDenseParentSlot(SceneState& state, SceneEntity entity) {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        if (state.denseHierarchyParents.size() <= index) {
            state.denseHierarchyParents.resize(static_cast<std::size_t>(index) + 1U);
        }
    }

    static void EnsureDenseChildrenSlot(SceneState& state, SceneEntity entity) {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        if (state.denseHierarchyChildren.size() <= index) {
            state.denseHierarchyChildren.resize(static_cast<std::size_t>(index) + 1U);
        }
    }

    static void SetDenseParent(SceneState& state, SceneEntity entity, SceneEntity parent) {
        EnsureDenseParentSlot(state, entity);
        const std::uint32_t index = DenseIndex(entity);
        if (index != kb::ecs::kInvalidGeneratedEntityIndex) {
            state.denseHierarchyParents[index] = parent;
        }
    }

    static void EnsureDenseOrderSlot(SceneState& state, SceneEntity entity) {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        if (state.denseHierarchyOrder.size() <= index) {
            state.denseHierarchyOrder.resize(static_cast<std::size_t>(index) + 1U);
        }
    }

    static void SetOrder(SceneState& state, SceneEntity entity, std::uint64_t order) {
        EnsureDenseOrderSlot(state, entity);
        const std::uint32_t index = DenseIndex(entity);
        if (index != kb::ecs::kInvalidGeneratedEntityIndex) {
            state.denseHierarchyOrder[index] = order;
            return;
        }
        state.hierarchyOrder[entity.Id()] = order;
    }

    static void AddDenseChild(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (!parent.IsValid()) {
            return;
        }
        EnsureDenseChildrenSlot(state, parent);
        const std::uint32_t index = DenseIndex(parent);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        state.denseHierarchyChildren[index].push_back(child);
    }

    [[nodiscard]] static const SceneEntity* DenseParent(const SceneState& state, SceneEntity entity) noexcept {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex || index >= state.denseHierarchyParents.size()) {
            return nullptr;
        }
        const SceneEntity& parent = state.denseHierarchyParents[index];
        return parent.IsValid() ? &parent : nullptr;
    }

    [[nodiscard]] static const std::vector<SceneEntity>* DenseChildren(const SceneState& state, SceneEntity entity) noexcept {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex || index >= state.denseHierarchyChildren.size() || state.denseHierarchyChildren[index].empty()) {
            return nullptr;
        }
        return &state.denseHierarchyChildren[index];
    }

    static void ClearDenseEntry(SceneState& state, SceneEntity entity) {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        if (index < state.denseHierarchyParents.size()) {
            state.denseHierarchyParents[index] = {};
        }
        if (index < state.denseHierarchyChildren.size()) {
            state.denseHierarchyChildren[index].clear();
        }
        if (index < state.denseHierarchyOrder.size()) {
            state.denseHierarchyOrder[index] = 0U;
        }
    }

    static void AddToParentList(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (parent.IsValid()) {
            AppendUnique(state.hierarchyChildren[parent.Id()], child);
            AddDenseChild(state, parent, child);
        } else {
            AppendUnique(state.hierarchyRoots, child);
        }
    }

    static void RemoveFromParentList(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (parent.IsValid()) {
            const auto children = state.hierarchyChildren.find(parent.Id());
            if (children == state.hierarchyChildren.end()) {
                RemoveDenseChild(state, parent, child);
                return;
            }
            Erase(children->second, child);
            if (children->second.empty()) {
                state.hierarchyChildren.erase(children);
            }
            RemoveDenseChild(state, parent, child);
        } else {
            Erase(state.hierarchyRoots, child);
        }
    }

    static void RemoveDenseChild(SceneState& state, SceneEntity parent, SceneEntity child) {
        const std::uint32_t index = DenseIndex(parent);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex || index >= state.denseHierarchyChildren.size()) {
            return;
        }
        Erase(state.denseHierarchyChildren[index], child);
    }

    static void AppendUnique(std::vector<SceneEntity>& entities, SceneEntity entity) {
        if (std::ranges::find(entities, entity) == entities.end()) {
            entities.push_back(entity);
        }
    }

    static void Erase(std::vector<SceneEntity>& entities, SceneEntity entity) {
        std::erase(entities, entity);
    }
};

} // namespace kb::scene
