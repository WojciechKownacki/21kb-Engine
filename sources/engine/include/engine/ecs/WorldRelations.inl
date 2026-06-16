#pragma once

#include <typeindex>

namespace kb::ecs {

template <typename T>
RelationId World::RegisterRelation(std::string_view name) {
    ValidateTagType<T>();
    return RegisterRelation(std::type_index{ typeid(T) }, name.empty() ? DefaultTagName<T>() : name);
}

template <typename T>
RelationId World::Relation() const noexcept {
    ValidateTagType<T>();
    return FindRelation(std::type_index{ typeid(T) });
}

template <typename T>
void World::AddRelation(Entity entity, Entity target) {
    AddRelation(entity, RegisterRelation<T>(), target);
}

template <typename T>
bool World::HasRelation(Entity entity, Entity target) const {
    ValidateEntityHandle(entity, "HasRelation");
    ValidateEntityHandle(target, "HasRelation");
    const RelationId relation = Relation<T>();
    return relation != 0 && HasRelation(entity, relation, target);
}

template <typename T>
void World::RemoveRelation(Entity entity, Entity target) {
    ValidateEntityHandle(entity, "RemoveRelation");
    ValidateEntityHandle(target, "RemoveRelation");
    const RelationId relation = Relation<T>();
    if (relation != 0) {
        RemoveRelation(entity, relation, target);
    }
}

template <typename T>
Entity World::RelationTarget(Entity entity, int index) const {
    ValidateEntityHandle(entity, "RelationTarget");
    const RelationId relation = Relation<T>();
    return relation == 0 ? Entity{} : RelationTarget(entity, relation, index);
}

} // namespace kb::ecs
