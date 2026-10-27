#include "ecs/relation/HierarchyDepthGuard.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <algorithm>
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

NewEntityParentBatchDepth ValidateNewEntityParentBatchDepth(
    ecs_world_t* world, std::span<const Entity> children, std::span<const Entity> parents) noexcept try {
    using Result = NewEntityParentBatchDepth;
    if (children.size() != parents.size()) return Result::Rejected;
    if (children.empty()) return Result::Fits;
    for (std::size_t index = 0U; index < children.size(); ++index) {
        const auto child = FlecsEntityId(children[index]);
        const auto parent = FlecsEntityId(parents[index]);
        if (!ecs_is_alive(world, child) || (parent != 0 && !ecs_is_alive(world, parent)) || child == parent ||
            ecs_get_parent(world, child) != 0 || !VisitChildren(world, child, [](ecs_entity_t) { return false; })) {
            return Result::RequiresGeneralAssignment;
        }
    }
    // The common flat batch needs neither a graph allocation nor repeated ancestor walks.
    if (std::all_of(parents.begin(), parents.end(), [&](Entity parent) { return parent == parents.front(); })) {
        return FitsHierarchyDepth(world, children.front(), parents.front()) ? Result::Fits : Result::Rejected;
    }
    const auto limit = HierarchyDepthLimit();
    const auto unknown = limit + 1U;
    struct Node {
        ecs_entity_t entity;
        ecs_entity_t parent;
        std::size_t depth;
        bool assigned;
        bool visiting = false;
    };
    std::vector<Node> nodes;
    if (children.size() > nodes.max_size() / 2U) return Result::Rejected;
    nodes.reserve(children.size() * 2U);
    for (std::size_t index = 0U; index < children.size(); ++index) {
        nodes.push_back({FlecsEntityId(children[index]), FlecsEntityId(parents[index]), unknown, true});
        if (parents[index].IsValid()) nodes.push_back({FlecsEntityId(parents[index]), 0U, unknown, false});
    }
    std::sort(nodes.begin(), nodes.end(), [](const Node& first, const Node& second) {
        return first.entity != second.entity ? first.entity < second.entity : first.assigned > second.assigned;
    });
    std::size_t unique = 0U;
    for (std::size_t index = 0U; index < nodes.size();) {
        const auto first = index++;
        while (index < nodes.size() && nodes[index].entity == nodes[first].entity) {
            if (nodes[index].assigned) return Result::RequiresGeneralAssignment;
            ++index;
        }
        nodes[unique++] = nodes[first];
    }
    nodes.resize(unique);
    for (auto& node : nodes) {
        if (!node.assigned) node.parent = ecs_strip_generation(ecs_get_parent(world, node.entity));
    }
    std::vector<std::size_t> path;
    path.reserve(std::min(nodes.size(), limit));
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        auto cursor = index;
        std::size_t depth = 0U;
        path.clear();
        for (;;) {
            auto& node = nodes[cursor];
            if (node.depth != unknown) { depth = node.depth; break; }
            if (node.visiting) return Result::Rejected;
            if (node.parent == 0U) { node.depth = 0U; break; }
            node.visiting = true;
            path.push_back(cursor);
            if (path.size() > limit) return Result::Rejected;
            const auto parent = std::lower_bound(nodes.begin(), nodes.end(), node.parent,
                [](const Node& candidate, ecs_entity_t entity) { return candidate.entity < entity; });
            if (parent != nodes.end() && parent->entity == node.parent) {
                cursor = static_cast<std::size_t>(parent - nodes.begin());
                continue;
            }
            for (auto ancestor = ecs_get_parent(world, node.parent); ancestor != 0U; ancestor = ecs_get_parent(world, ancestor)) {
                if (++depth > limit) return Result::Rejected;
            }
            break;
        }
        while (!path.empty()) {
            if (++depth > limit) return Result::Rejected;
            nodes[path.back()].depth = depth;
            nodes[path.back()].visiting = false;
            path.pop_back();
        }
    }
    return Result::Fits;
} catch (const std::bad_alloc&) {
    return NewEntityParentBatchDepth::Rejected;
}

} // namespace kb::ecs
