#include "engine/ecs/World.hpp"

#include "ecs/relation/HierarchyRelationService.hpp"
#include "ecs/relation/RelationStorage.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

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
    return RelationStorage::Target(world_, entity, relation, index);
}

void World::SetParent(Entity child, Entity parent) {
    ValidateEntityHandle(child, "SetParent");
    ValidateOptionalEntityHandle(parent, "SetParent");
    if (Parent(child) != parent) {
        ValidateStructuralChangeAllowed("SetParent");
    }
    ecs_table_t* previousArchetype = EntityArchetype(child);
    HierarchyRelationService::SetParent(world_, child, parent);
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(child));
}

void World::ClearParent(Entity child) {
    ValidateEntityHandle(child, "ClearParent");
    if (Parent(child).IsValid()) {
        ValidateStructuralChangeAllowed("ClearParent");
    }
    ecs_table_t* previousArchetype = EntityArchetype(child);
    HierarchyRelationService::ClearParent(world_, child);
    InvalidateQueryPlansForArchetypeChange(previousArchetype, EntityArchetype(child));
}

Entity World::Parent(Entity child) const {
    ValidateEntityHandle(child, "Parent");
    return HierarchyRelationService::Parent(world_, child);
}

std::vector<Entity> World::Children(Entity parent) const {
    ValidateEntityHandle(parent, "Children");
    return HierarchyRelationService::Children(world_, parent);
}

} // namespace kb::ecs
