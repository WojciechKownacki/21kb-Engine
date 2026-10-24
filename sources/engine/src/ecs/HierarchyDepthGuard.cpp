#include "ecs/relation/HierarchyDepthGuard.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <optional>
#include <new>
#include <vector>

namespace kb::ecs {
namespace {

struct ChildIterator {
    ecs_iter_t value;
    bool active = true;
    ~ChildIterator() { if (active) ecs_iter_fini(&value); }
};

template <typename Visitor>
bool VisitChildren(ecs_world_t* world, ecs_entity_t parent, Visitor&& visit) {
    ChildIterator iterator{ecs_children(world, parent)};
    while (ecs_children_next(&iterator.value)) {
        for (int32_t row = 0; row < iterator.value.count; ++row) {
            if (!visit(iterator.value.entities[row])) return false;
        }
    }
    iterator.active = false;
    return true;
}

} // namespace

std::size_t HierarchyDepthLimit() noexcept {
    return KB_ECS_HIERARCHY_DEPTH_LIMIT;
}

bool FitsHierarchyDepth(ecs_world_t* world, Entity child, Entity parent) noexcept try {
    if (!parent.IsValid()) return true;
    const auto limit = HierarchyDepthLimit();
    std::size_t depth = 0U;
    for (auto ancestor = FlecsEntityId(parent); ancestor != 0; ancestor = ecs_get_parent(world, ancestor)) {
        if (++depth > limit) return false;
    }
    struct Pending { ecs_entity_t entity; std::size_t remaining; };
    // Leaf assignments do not need traversal storage, including in checked library builds.
    std::optional<std::vector<Pending>> pending;
    const auto enqueueChildren = [&](ecs_entity_t entity, std::size_t remaining) {
        return VisitChildren(world, entity, [&](ecs_entity_t descendant) {
            if (remaining == 0U) return false;
            if (!pending) pending.emplace();
            pending->push_back({descendant, remaining - 1U});
            return true;
        });
    };
    if (!enqueueChildren(FlecsEntityId(child), limit - depth)) return false;
    while (pending && !pending->empty()) {
        const auto current = pending->back();
        pending->pop_back();
        if (!enqueueChildren(current.entity, current.remaining)) return false;
    }
    return true;
} catch (const std::bad_alloc&) {
    return false;
}

} // namespace kb::ecs
