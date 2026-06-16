#include "engine/ecs/World.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/relation/HierarchyRelationService.hpp"
#include "ecs/relation/RelationStorage.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <unordered_set>

namespace kb::ecs {

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
    const ecs_entity_t childId = FlecsEntityId(child);
    for (ecs_entity_t ancestor = FlecsEntityId(parent); ancestor != 0; ancestor = ecs_get_parent(world_, ancestor)) {
        if (ancestor == childId) {
            return;
        }
        if (!visitedAncestors.insert(ancestor).second) {
            return;
        }
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

Entity World::Parent(Entity child) const {
    ValidateEntityHandle(child, "Parent");
    const Entity parent = HierarchyRelationService::Parent(world_, child);
    return ResolveAliveEntity(parent.Id());
}

std::vector<Entity> World::Children(Entity parent) const {
    ValidateEntityHandle(parent, "Children");
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
