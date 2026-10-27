#include "scene/hierarchy/SceneHierarchyDepthGuard.hpp"

#include "ecs/relation/HierarchyDepthGuard.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"

#include <new>
#include <optional>
#include <vector>

namespace kb::scene {

std::size_t SceneChildDepth(const SceneState& state, SceneEntity parent) noexcept {
    const auto limit = kb::ecs::HierarchyDepthLimit();
    std::size_t depth = 0U;
    while (parent.IsValid()) {
        if (++depth > limit) return depth;
        parent = SceneHierarchyCache::Parent(state, parent);
    }
    return depth;
}

bool FitsSceneHierarchyDepth(const SceneState& state, SceneEntity child, SceneEntity parent) noexcept try {
    if (!parent.IsValid()) return true;
    const auto limit = kb::ecs::HierarchyDepthLimit();
    const auto depth = SceneChildDepth(state, parent);
    if (depth > limit) return false;
    struct Pending { SceneEntity entity; std::size_t remaining; };
    std::optional<std::vector<Pending>> pending;
    Pending current{child, limit - depth};
    for (;;) {
        const auto count = SceneHierarchyCache::ChildCount(state, current.entity);
        if (count != 0U) {
            if (current.remaining == 0U) return false;
            if (!pending) pending.emplace();
            for (std::size_t index = 0U; index < count; ++index) {
                pending->push_back({SceneHierarchyCache::ChildAt(state, current.entity, index), current.remaining - 1U});
            }
        }
        if (!pending || pending->empty()) return true;
        current = pending->back();
        pending->pop_back();
    }
} catch (const std::bad_alloc&) {
    return false;
}

} // namespace kb::scene
