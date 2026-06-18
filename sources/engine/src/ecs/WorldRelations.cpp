#include "engine/ecs/World.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/relation/HierarchyRelationService.hpp"
#include "ecs/relation/RelationStorage.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <cstddef>
#include <stdexcept>
#include <unordered_set>

namespace kb::ecs {
namespace {

[[nodiscard]] bool WouldCreateParentCycle(
    const ecs_world_t* world,
    Entity child,
    Entity parent,
    std::unordered_set<ecs_entity_t>& visitedAncestors) noexcept {
    visitedAncestors.clear();
    const ecs_entity_t childId = FlecsEntityId(child);
    for (ecs_entity_t ancestor = FlecsEntityId(parent); ancestor != 0; ancestor = ecs_get_parent(world, ancestor)) {
        if (ancestor == childId) {
            return true;
        }
        if (!visitedAncestors.insert(ancestor).second) {
            return true;
        }
    }
    return false;
}

} // namespace

RelationId World::RegisterRelation(std::type_index type, std::string_view name) {
    return registries_ == nullptr ? 0 : registries_->Relations().Register(world_, type, name);
}

RelationId World::FindRelation(std::type_index type) const noexcept {
    return registries_ == nullptr ? 0 : registries_->Relations().Find(type);
}

void World::AddRelation(Entity entity, RelationId relation, Entity target) {
    ValidateEntityHandle(entity, "AddRelation");
    ValidateEntityHandle(target, "AddRelation");
    if (relation != 0 && !HasRelation(entity, relation, target)) {
        ValidateStructuralChangeAllowed("AddRelation");
    }
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    RelationStorage::Add(world_, entity, relation, target);
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
}

bool World::HasRelation(Entity entity, RelationId relation, Entity target) const {
    ValidateEntityHandle(entity, "HasRelation");
    ValidateEntityHandle(target, "HasRelation");
    return RelationStorage::Has(world_, entity, relation, target);
}

void World::RemoveRelation(Entity entity, RelationId relation, Entity target) {
    ValidateEntityHandle(entity, "RemoveRelation");
    ValidateEntityHandle(target, "RemoveRelation");
    if (relation != 0 && HasRelation(entity, relation, target)) {
        ValidateStructuralChangeAllowed("RemoveRelation");
    }
    ecs_table_t* previousArchetype = EntityArchetype(entity);
    RelationStorage::Remove(world_, entity, relation, target);
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(entity));
}

Entity World::RelationTarget(Entity entity, RelationId relation, int index) const {
    ValidateEntityHandle(entity, "RelationTarget");
    const Entity target = RelationStorage::Target(world_, entity, relation, index);
    return ResolveAliveEntity(target.Id());
}

void World::SetParent(Entity child, Entity parent) {
    ValidateEntityHandle(child, "SetParent");
    ValidateOptionalEntityHandle(parent, "SetParent");
    std::unordered_set<ecs_entity_t> visitedAncestors;
    if (WouldCreateParentCycle(world_, child, parent, visitedAncestors)) {
        return;
    }
    ValidateStructuralChangeAllowed("SetParent");
    ecs_table_t* previousArchetype = EntityArchetype(child);
    const Entity currentParent = ResolveAliveEntity(RelationStorage::Target(world_, child, EcsChildOf).Id());
    if (currentParent.IsValid()) {
        RelationStorage::Remove(world_, child, EcsChildOf, currentParent);
    }
    if (parent.IsValid() && child != parent) {
        RelationStorage::Add(world_, child, EcsChildOf, parent);
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(child));
}

void World::SetParents(std::span<const Entity> children, std::span<const Entity> parents) {
    if (children.size() != parents.size()) {
        throw std::invalid_argument("SetParents requires matching child and parent counts");
    }
    if (children.empty()) {
        return;
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        ValidateEntityHandle(children[index], "SetParents");
        ValidateOptionalEntityHandle(parents[index], "SetParents");
    }

    ValidateStructuralChangeAllowed("SetParents");
    std::unordered_set<ecs_entity_t> visitedAncestors;
    bool changed = false;
    for (std::size_t index = 0; index < children.size(); ++index) {
        const Entity child = children[index];
        const Entity parent = parents[index];
        if (child == parent || WouldCreateParentCycle(world_, child, parent, visitedAncestors)) {
            continue;
        }

        const Entity currentParent = ResolveAliveEntity(RelationStorage::Target(world_, child, EcsChildOf).Id());
        if (currentParent == parent) {
            continue;
        }
        if (currentParent.IsValid()) {
            RelationStorage::Remove(world_, child, EcsChildOf, currentParent);
        }
        if (parent.IsValid()) {
            RelationStorage::Add(world_, child, EcsChildOf, parent);
        }
        changed = true;
    }
    if (changed) {
        InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
    }
}

void World::SetParentsForNewEntitiesKnownAcyclic(std::span<const Entity> children, std::span<const Entity> parents) {
    if (children.size() != parents.size()) {
        throw std::invalid_argument("SetParentsForNewEntitiesKnownAcyclic requires matching child and parent counts");
    }
    if (children.empty()) {
        return;
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        ValidateEntityHandle(children[index], "SetParentsForNewEntitiesKnownAcyclic");
        ValidateOptionalEntityHandle(parents[index], "SetParentsForNewEntitiesKnownAcyclic");
    }

    ValidateStructuralChangeAllowed("SetParentsForNewEntitiesKnownAcyclic");
    const std::size_t added = RelationStorage::AddKnownAlivePairs(world_, children, EcsChildOf, parents);
    if (added > 0U) {
        InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
    }
}

void World::ClearParent(Entity child) {
    ValidateEntityHandle(child, "ClearParent");
    if (Parent(child).IsValid()) {
        ValidateStructuralChangeAllowed("ClearParent");
    }
    ecs_table_t* previousArchetype = EntityArchetype(child);
    const Entity currentParent = ResolveAliveEntity(RelationStorage::Target(world_, child, EcsChildOf).Id());
    if (currentParent.IsValid()) {
        RelationStorage::Remove(world_, child, EcsChildOf, currentParent);
    }
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(child));
}

void World::ClearParents(std::span<const Entity> children) {
    if (children.empty()) {
        return;
    }

    bool hasParentToClear = false;
    for (Entity child : children) {
        ValidateEntityHandle(child, "ClearParents");
        hasParentToClear = hasParentToClear || RelationStorage::Target(world_, child, EcsChildOf).Id() != 0U;
    }

    if (hasParentToClear) {
        ValidateStructuralChangeAllowed("ClearParents");
    }

    bool changed = false;
    for (Entity child : children) {
        const Entity currentParent = ResolveAliveEntity(RelationStorage::Target(world_, child, EcsChildOf).Id());
        if (!currentParent.IsValid()) {
            continue;
        }
        RelationStorage::Remove(world_, child, EcsChildOf, currentParent);
        changed = true;
    }
    if (changed) {
        InvalidateQueryPlansForArchetypeChange(nullptr, nullptr);
    }
}

Entity World::Parent(Entity child) const {
    ValidateEntityHandle(child, "Parent");
    if (world_ == nullptr || !ecs_is_alive(world_, FlecsEntityId(child))) {
        return {};
    }
    const Entity parent = HierarchyRelationService::Parent(world_, child);
    return ResolveAliveEntity(parent.Id());
}

std::vector<Entity> World::Children(Entity parent) const {
    ValidateEntityHandle(parent, "Children");
    if (world_ == nullptr || !ecs_is_alive(world_, FlecsEntityId(parent))) {
        return {};
    }
    std::vector<Entity> children = HierarchyRelationService::Children(world_, parent);
    std::vector<Entity> resolvedChildren;
    resolvedChildren.reserve(children.size());
    for (Entity child : children) {
        if (Entity resolved = ResolveAliveEntity(child.Id()); resolved.IsValid()) {
            resolvedChildren.push_back(resolved);
        }
    }
    return resolvedChildren;
}

} // namespace kb::ecs
