#include "engine/ecs/World.hpp"

#include "ecs/relation/HierarchyRelationService.hpp"
#include "ecs/relation/RelationStorage.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"

namespace kb::ecs {

RelationId World::RegisterRelation(std::type_index type, std::string_view name) {
    return relations_ == nullptr ? 0 : relations_->Register(world_, type, name);
}

RelationId World::FindRelation(std::type_index type) const noexcept {
    return relations_ == nullptr ? 0 : relations_->Find(type);
}

void World::AddRelation(Entity entity, RelationId relation, Entity target) noexcept {
    RelationStorage::Add(world_, entity, relation, target);
}

bool World::HasRelation(Entity entity, RelationId relation, Entity target) const noexcept {
    return RelationStorage::Has(world_, entity, relation, target);
}

void World::RemoveRelation(Entity entity, RelationId relation, Entity target) noexcept {
    RelationStorage::Remove(world_, entity, relation, target);
}

Entity World::RelationTarget(Entity entity, RelationId relation, int index) const noexcept {
    return RelationStorage::Target(world_, entity, relation, index);
}

void World::SetParent(Entity child, Entity parent) noexcept {
    HierarchyRelationService::SetParent(world_, child, parent);
}

void World::ClearParent(Entity child) noexcept {
    HierarchyRelationService::ClearParent(world_, child);
}

Entity World::Parent(Entity child) const noexcept {
    return HierarchyRelationService::Parent(world_, child);
}

} // namespace kb::ecs
