#include "scene/transform/SceneTransformTopologyCache.hpp"

#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

namespace kb::scene {

const SceneTransformTopologyCache::Location* SceneTransformTopologyCache::Find(SceneEntity entity) const noexcept {
    const auto index = kb::ecs::GeneratedEntityIndex(entity);
    if (index != kb::ecs::kInvalidGeneratedEntityIndex) {
        return index < dense_.size() && dense_[index].entity == entity ? &dense_[index] : nullptr;
    }
    const auto found = sparse_.find(entity.Id());
    return found == sparse_.end() ? nullptr : &found->second;
}

void SceneTransformTopologyCache::Store(Location location) {
    const auto index = kb::ecs::GeneratedEntityIndex(location.entity);
    if (index == kb::ecs::kInvalidGeneratedEntityIndex) {
        sparse_[location.entity.Id()] = location;
    } else {
        if (dense_.size() <= index) dense_.resize(static_cast<std::size_t>(index) + 1U);
        dense_[index] = location;
    }
}

void SceneTransformTopologyCache::Append(SceneEntity entity, std::size_t level) {
    if (levels_.size() <= level) levels_.resize(level + 1U);
    const auto offset = levels_[level].size();
    levels_[level].push_back(entity);
    Store({entity, level, offset});
}

void SceneTransformTopologyCache::Erase(SceneEntity entity) noexcept {
    const auto* found = Find(entity);
    if (found == nullptr) return;
    const auto location = *found;
    auto& level = levels_[location.level];
    const auto moved = level.back();
    level[location.offset] = moved;
    level.pop_back();
    if (moved != entity) {
        // Both locations already exist: deleting a leaf never grows storage.
        const auto movedIndex = kb::ecs::GeneratedEntityIndex(moved);
        if (movedIndex != kb::ecs::kInvalidGeneratedEntityIndex) dense_[movedIndex].offset = location.offset;
        else sparse_.find(moved.Id())->second.offset = location.offset;
    }
    const auto index = kb::ecs::GeneratedEntityIndex(entity);
    if (index != kb::ecs::kInvalidGeneratedEntityIndex) dense_[index] = {};
    else sparse_.erase(entity.Id());
    while (!levels_.empty() && levels_.back().empty()) levels_.pop_back();
}

void SceneTransformTopologyCache::Refresh(const SceneState& state) {
    if (version_ == state.hierarchyTopologyVersion) return;
    version_ = 0U;
    levels_.clear();
    dense_.clear();
    sparse_.clear();
    for (const auto root : state.hierarchyRoots) Append(root, 0U);
    for (std::size_t level = 0U; level < levels_.size(); ++level) {
        // Index by offset: appending a new level can relocate levels_.
        for (std::size_t offset = 0U; offset < levels_[level].size(); ++offset) {
            const auto entity = levels_[level][offset];
            const auto count = SceneHierarchyCache::ChildCount(state, entity);
            for (std::size_t child = 0U; child < count; ++child) {
                Append(SceneHierarchyCache::ChildAt(state, entity, child), level + 1U);
            }
        }
    }
    version_ = state.hierarchyTopologyVersion;
    ++buildCount_;
}

void SceneTransformTopologyCache::Added(const SceneState& state, std::span<const SceneEntity> entities, std::uint64_t previousVersion) {
    if (version_ != previousVersion) { version_ = 0U; return; }
    version_ = 0U;
    for (const auto entity : entities) {
        if (Find(entity) != nullptr) return;
        const auto parent = SceneHierarchyCache::Parent(state, entity);
        const auto* parentLocation = parent.IsValid() ? Find(parent) : nullptr;
        if (parent.IsValid() && parentLocation == nullptr) return;
        const auto level = parentLocation == nullptr ? 0U : parentLocation->level + 1U;
        Append(entity, level);
    }
    version_ = state.hierarchyTopologyVersion;
}

void SceneTransformTopologyCache::Removed(const SceneState& state, SceneEntity entity, std::uint64_t previousVersion) noexcept {
    if (version_ != previousVersion) { version_ = 0U; return; }
    Erase(entity);
    version_ = state.hierarchyTopologyVersion;
}

void SceneTransformTopologyCache::Moved(const SceneState& state, SceneEntity entity, std::uint64_t previousVersion) {
    if (version_ != previousVersion || SceneHierarchyCache::ChildCount(state, entity) != 0U) {
        version_ = 0U;
        return;
    }
    version_ = 0U;
    const auto parent = SceneHierarchyCache::Parent(state, entity);
    const auto* parentLocation = parent.IsValid() ? Find(parent) : nullptr;
    if (parent.IsValid() && parentLocation == nullptr) return;
    const auto level = parentLocation == nullptr ? 0U : parentLocation->level + 1U;
    Erase(entity);
    Append(entity, level);
    version_ = state.hierarchyTopologyVersion;
}

void SceneTransformTopologyCache::LeafRemovalHandled(std::uint64_t before, std::uint64_t after) noexcept {
    // A previous untracked removal must survive even when a known leaf is
    // destroyed afterwards. Only the scene destruction service certifies it.
    static_cast<void>(RemovalSafetyEpoch(before));
    nativeRemovalVersion_ = after;
}

std::uint64_t SceneTransformTopologyCache::RemovalSafetyEpoch(std::uint64_t currentRemovalVersion) noexcept {
    if (nativeRemovalVersion_ != currentRemovalVersion) {
        nativeRemovalVersion_ = currentRemovalVersion;
        if (++removalSafetyEpoch_ == 0U) ++removalSafetyEpoch_;
    }
    return removalSafetyEpoch_;
}

void SceneTransformTopologyCache::MetadataChanged(std::uint64_t previousVersion, std::uint64_t currentVersion) noexcept {
    if (version_ == previousVersion) version_ = currentVersion;
}

} // namespace kb::scene
