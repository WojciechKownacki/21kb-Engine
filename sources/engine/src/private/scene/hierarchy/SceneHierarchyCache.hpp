#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "ecs/GeometricReserve.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace kb::scene {

class SceneHierarchyCache {
public:
    SceneHierarchyCache() = delete;

    static void Add(SceneState& state, SceneEntity entity, SceneEntity parent) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        state.hierarchyParents[entity.Id()] = parent;
        SetDenseParent(state, entity, parent);
        AddToParentList(state, parent, entity);
        RefreshTransformLink(state, entity);
        RefreshTransformLink(state, parent);
        MarkTopologyDirty(state);
        state.transformTopology.Added(state, {&entity, 1U}, previousVersion);
    }

    static void AddRoot(SceneState& state, SceneEntity entity) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        // Entity creation calls this once for a fresh entity. Moving an existing
        // entity into the root list still goes through Move.
        if (!state.hierarchyParents.emplace(entity.Id(), SceneEntity{}).second) {
            throw std::logic_error("Scene hierarchy root already registered");
        }
        SetDenseParent(state, entity, {});
        state.hierarchyRoots.push_back(entity);
        NoteRootAppended(state, entity);
        MarkTopologyDirty(state, true);
        state.transformTopology.Added(state, {&entity, 1U}, previousVersion);
    }

    // AddRoot for a batch of fresh entities. Only an entity without a dense slot is kept in the hash map, as
    // AddManyDense does; each root still moves the topology version by one, so the batch reads as appended roots.
    static void AddRoots(SceneState& state, std::span<const SceneEntity> entities) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        kb::ecs::ReserveGeometric(state.hierarchyRoots, state.hierarchyRoots.size() + entities.size());
        for (const SceneEntity entity : entities) {
            if (DenseIndex(entity) == kb::ecs::kInvalidGeneratedEntityIndex && !state.hierarchyParents.emplace(entity.Id(), SceneEntity{}).second) {
                throw std::logic_error("Scene hierarchy root already registered");
            }
            SetDenseParent(state, entity, {});
            state.hierarchyRoots.push_back(entity);
            NoteRootAppended(state, entity);
            MarkTopologyDirty(state, true);
        }
        state.transformTopology.Added(state, entities, previousVersion);
    }

    static void AssignOrder(SceneState& state, SceneEntity entity) {
        SetOrder(state, entity, state.nextHierarchyOrder++);
    }

    [[nodiscard]] static SceneEntity Parent(const SceneState& state, SceneEntity entity) noexcept {
        // An entity with a slot in the dense table is registered there by every writer (the hash map is only the store of
        // entities without a dense index), so a root's empty slot is the answer: no second lookup in the hash map.
        const std::uint32_t denseIndex = DenseIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyParents.size()) {
            return state.denseHierarchyParents[denseIndex];
        }
        const auto parent = state.hierarchyParents.find(entity.Id());
        return parent == state.hierarchyParents.end() ? SceneEntity{} : parent->second;
    }

    static void AddMany(SceneState& state, std::span<const SceneEntity> entities, std::span<const SceneEntity> parents) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        if (entities.size() != parents.size()) {
            throw std::invalid_argument("Scene hierarchy cache bulk add requires matching entity and parent counts");
        }

        state.hierarchyParents.reserve(state.hierarchyParents.size() + entities.size());
        state.hierarchyChildren.reserve(state.hierarchyChildren.size() + entities.size());
        kb::ecs::ReserveGeometric(state.hierarchyRoots, state.hierarchyRoots.size() + entities.size());
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
                NoteRootAppended(state, entity);
            }
        }
        RefreshTransformLinks(state, entities, parents);
        MarkTopologyDirty(state);
        state.transformTopology.Added(state, entities, previousVersion);
    }

    static void AddManyDense(SceneState& state, std::span<const SceneEntity> entities, std::span<const SceneEntity> parents) {
        const auto previousVersion = state.hierarchyTopologyVersion;
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

        kb::ecs::ReserveGeometric(state.hierarchyRoots, state.hierarchyRoots.size() + rootCount);
        for (std::uint32_t index = 0; index < childCounts.size(); ++index) {
            if (childCounts[index] != 0U) {
                kb::ecs::ReserveGeometric(state.denseHierarchyChildren[index], state.denseHierarchyChildren[index].size() + childCounts[index]);
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
                NoteRootAppended(state, entity);
                continue;
            }

            const std::uint32_t parentIndex = DenseIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyChildren[parentIndex].push_back(entity);
            } else {
                state.hierarchyChildren[parent.Id()].push_back(entity);
            }
        }
        RefreshTransformLinks(state, entities, parents);
        MarkTopologyDirty(state);
        state.transformTopology.Added(state, entities, previousVersion);
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
        const auto previousVersion = state.hierarchyTopologyVersion;
        RemoveFromParentList(state, oldParent, child);
        state.hierarchyParents[child.Id()] = newParent;
        SetDenseParent(state, child, newParent);
        AddToParentList(state, newParent, child);
        RefreshTransformLink(state, child);
        RefreshTransformLink(state, oldParent);
        RefreshTransformLink(state, newParent);
        MarkTopologyDirty(state);
        state.transformTopology.Moved(state, child, previousVersion);
    }

    static void Remove(SceneState& state, SceneEntity entity, SceneEntity parent) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        RemoveFromParentList(state, parent, entity);
        state.hierarchyParents.erase(entity.Id());
        state.hierarchyChildren.erase(entity.Id());
        state.hierarchyOrder.erase(entity.Id());
        state.hierarchyRootSlot.erase(entity.Id());
        ClearDenseEntry(state, entity);
        RefreshTransformLink(state, entity);
        RefreshTransformLink(state, parent);
        MarkTopologyDirty(state);
        state.transformTopology.Removed(state, entity, previousVersion);
    }

    [[nodiscard]] static std::vector<SceneEntity> Roots(const SceneState& state) {
        if (state.hierarchyRootHoles == 0U) {
            return state.hierarchyRoots;
        }
        std::vector<SceneEntity> roots;
        roots.reserve(RootCount(state));
        for (const SceneEntity root : state.hierarchyRoots) {
            if (root.IsValid()) {
                roots.push_back(root);
            }
        }
        return roots;
    }

    [[nodiscard]] static std::size_t RootCount(const SceneState& state) noexcept {
        return state.hierarchyRoots.size() - state.hierarchyRootHoles;
    }

    [[nodiscard]] static SceneEntity RootAt(const SceneState& state, std::size_t index) noexcept {
        const std::vector<SceneEntity>& roots = state.hierarchyRoots;
        const std::size_t count = RootCount(state);
        if (index >= count) {
            return {};
        }
        if (state.hierarchyRootHoles == 0U) {
            return roots[index];
        }
        // The roots after the last hole stand together at the end of the list, where a pass that reads what was appended
        // since its last one looks.
        if (index + (roots.size() - 1U - state.hierarchyRootLastHole) >= count) {
            return roots[roots.size() - (count - index)];
        }
        std::size_t remaining = index;
        for (const SceneEntity root : roots) {
            if (root.IsValid() && remaining-- == 0U) {
                return root;
            }
        }
        return {};
    }

    [[nodiscard]] static std::uint64_t RootAppendEpoch(const SceneState& state) noexcept {
        return state.hierarchyRootAppendEpoch;
    }

    static void MarkRowContentDirty(SceneState& state) noexcept {
        ++state.hierarchyRootAppendEpoch;
        if (state.hierarchyRootAppendEpoch == 0U) {
            state.hierarchyRootAppendEpoch = 1U;
        }
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
        const std::uint32_t denseIndex = DenseIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < state.denseHierarchyChildren.size()) {
            return state.denseHierarchyChildren[denseIndex].size();   // a leaf's empty slot is the answer
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

    // Whether the entity has a parent or children, from the link bits for a dense index (safe to read from the
    // transform workers) and from the tables otherwise.
    [[nodiscard]] static bool HasTransformLink(const SceneState& state, SceneEntity entity) noexcept {
        const std::uint32_t denseIndex = DenseIndex(entity);
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
            return Parent(state, entity).IsValid() || ChildCount(state, entity) != 0U;
        }
        const std::size_t word = denseIndex / 64U;
        return word < state.transformLinkBits.size() && (state.transformLinkBits[word] >> (denseIndex % 64U) & 1U) != 0U;
    }

    static void RefreshTransformLink(SceneState& state, SceneEntity entity) {
        const std::uint32_t denseIndex = DenseIndex(entity);
        if (!entity.IsValid() || denseIndex == kb::ecs::kInvalidGeneratedEntityIndex) {
            return;
        }
        const bool linked = Parent(state, entity).IsValid() || ChildCount(state, entity) != 0U;
        const std::size_t word = denseIndex / 64U;
        if (word >= state.transformLinkBits.size()) {
            if (!linked) {
                return;
            }
            state.transformLinkBits.resize(word + 1U, 0U);
        }
        const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
        state.transformLinkBits[word] = linked ? state.transformLinkBits[word] | bit : state.transformLinkBits[word] & ~bit;
    }

    // Records that the entity was just appended to hierarchyRoots.
    static void NoteRootAppended(SceneState& state, SceneEntity entity) {
        NoteRootSlot(state, entity, state.hierarchyRoots.size() - 1U);
    }

    // Records where the entity stands in hierarchyRoots.
    static void NoteRootSlot(SceneState& state, SceneEntity entity, std::size_t position) {
        StoreRootSlot(state, entity, static_cast<std::uint64_t>(position) + 1U);
    }

    // Orders roots as hierarchyRoots does; 0 for an entity that is not a root.
    [[nodiscard]] static std::uint64_t RootSlot(const SceneState& state, SceneEntity entity) noexcept {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            const auto slot = state.hierarchyRootSlot.find(entity.Id());
            return slot == state.hierarchyRootSlot.end() ? 0U : slot->second;
        }
        return index < state.denseHierarchyRootSlot.size() ? state.denseHierarchyRootSlot[index] : 0U;
    }

    static void RefreshTransformLinks(SceneState& state, std::span<const SceneEntity> entities, std::span<const SceneEntity> parents) {
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            RefreshTransformLink(state, entities[index]);
            if (index < parents.size()) {
                RefreshTransformLink(state, parents[index]);
            }
        }
    }

    // Puts the children of `parent` (the roots for an invalid parent) in the order of `ordered`, which lists the same
    // entities. Only sibling order changes, which the transform pass does not depend on.
    static void ReorderChildren(SceneState& state, SceneEntity parent, std::span<const SceneEntity> ordered) {
        const auto previousVersion = state.hierarchyTopologyVersion;
        const auto reorder = [ordered](std::vector<SceneEntity>& children) {
            std::unordered_set<SceneEntity::IdType> members;
            members.reserve(children.size());
            for (const SceneEntity entity : children) {
                members.insert(entity.Id());
            }
            std::vector<SceneEntity> reordered;
            reordered.reserve(children.size());
            for (const SceneEntity entity : ordered) {
                if (members.contains(entity.Id())) {
                    reordered.push_back(entity);
                }
            }
            if (reordered.size() == children.size()) {
                children = std::move(reordered);
            }
        };
        if (!parent.IsValid()) {
            CompactRoots(state);
            reorder(state.hierarchyRoots);
            for (std::size_t position = 0U; position < state.hierarchyRoots.size(); ++position) {
                NoteRootSlot(state, state.hierarchyRoots[position], position);
            }
        } else {
            if (const auto children = state.hierarchyChildren.find(parent.Id()); children != state.hierarchyChildren.end()) {
                reorder(children->second);
            }
            const std::uint32_t index = DenseIndex(parent);
            if (index != kb::ecs::kInvalidGeneratedEntityIndex && index < state.denseHierarchyChildren.size()) {
                reorder(state.denseHierarchyChildren[index]);
            }
        }
        MarkTopologyDirty(state);
        state.transformTopology.MetadataChanged(previousVersion, state.hierarchyTopologyVersion);
    }

private:
    static void MarkTopologyDirty(SceneState& state, bool rootAppendOnly = false) noexcept {
        ++state.hierarchyTopologyVersion;
        if (state.hierarchyTopologyVersion == 0U) {
            state.hierarchyTopologyVersion = 1U;
        }
        if (!rootAppendOnly) {
            MarkRowContentDirty(state);
        }
        ++state.renderTopologyVersion;
        if (state.renderTopologyVersion == 0U) {
            state.renderTopologyVersion = 1U;
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
        if (index < state.denseHierarchyRootSlot.size()) {
            state.denseHierarchyRootSlot[index] = 0U;
        }
    }

    // The child has just left the list of its old parent (or is new), so it is not in this list yet: no search.
    static void AddToParentList(SceneState& state, SceneEntity parent, SceneEntity child) {
        if (parent.IsValid()) {
            state.hierarchyChildren[parent.Id()].push_back(child);
            AddDenseChild(state, parent, child);
        } else if (RootSlot(state, child) == 0U) {
            state.hierarchyRoots.push_back(child);
            NoteRootAppended(state, child);
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
            RemoveRoot(state, child);
        }
    }

    // Leaves a hole in the place of the root, so the other roots keep their order and slots. The last root is popped, and
    // the holes are closed once they are half of the list: the cost of a removal does not depend on the number of roots.
    static void RemoveRoot(SceneState& state, SceneEntity root) {
        const std::uint64_t slot = RootSlot(state, root);
        if (slot == 0U) {
            return;
        }
        std::vector<SceneEntity>& roots = state.hierarchyRoots;
        const std::size_t position = static_cast<std::size_t>(slot - 1U);
        if (position >= roots.size() || roots[position] != root) {
            throw std::logic_error("Scene hierarchy root slot is out of sync");
        }
        StoreRootSlot(state, root, 0U);
        if (position + 1U == roots.size()) {
            roots.pop_back();
            return;
        }
        roots[position] = SceneEntity{};
        state.hierarchyRootLastHole = state.hierarchyRootHoles == 0U ? position : std::max(state.hierarchyRootLastHole, position);
        ++state.hierarchyRootHoles;
        if (state.hierarchyRootHoles * 2U > roots.size()) {
            CompactRoots(state);
        }
    }

    static void CompactRoots(SceneState& state) {
        if (state.hierarchyRootHoles == 0U) {
            return;
        }
        std::vector<SceneEntity>& roots = state.hierarchyRoots;
        std::size_t kept = 0U;
        for (std::size_t position = 0U; position < roots.size(); ++position) {
            const SceneEntity root = roots[position];
            if (!root.IsValid()) {
                continue;
            }
            if (kept != position) {
                roots[kept] = root;
                NoteRootSlot(state, root, kept);
            }
            ++kept;
        }
        roots.resize(kept);
        state.hierarchyRootHoles = 0U;
        state.hierarchyRootLastHole = 0U;
    }

    // `value` is the position in hierarchyRoots plus one, or 0 for an entity that leaves the list.
    static void StoreRootSlot(SceneState& state, SceneEntity entity, std::uint64_t value) {
        const std::uint32_t index = DenseIndex(entity);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
            if (value == 0U) {
                state.hierarchyRootSlot.erase(entity.Id());
            } else {
                state.hierarchyRootSlot[entity.Id()] = value;
            }
            return;
        }
        if (state.denseHierarchyRootSlot.size() <= index) {
            if (value == 0U) {
                return;
            }
            state.denseHierarchyRootSlot.resize(static_cast<std::size_t>(index) + 1U, 0U);
        }
        state.denseHierarchyRootSlot[index] = value;
    }

    static void RemoveDenseChild(SceneState& state, SceneEntity parent, SceneEntity child) {
        const std::uint32_t index = DenseIndex(parent);
        if (index == kb::ecs::kInvalidGeneratedEntityIndex || index >= state.denseHierarchyChildren.size()) {
            return;
        }
        Erase(state.denseHierarchyChildren[index], child);
    }

    static void Erase(std::vector<SceneEntity>& entities, SceneEntity entity) {
        // Hierarchy lists contain unique entities; the common subtree teardown case is the last child.
        if (!entities.empty() && entities.back() == entity) {
            entities.pop_back();
            return;
        }
        std::erase(entities, entity);
    }
};

} // namespace kb::scene
