#include "engine/ecs/World.hpp"

#include "ecs/relation/TagStorage.hpp"
#include "ecs/type/TagTypeRegistry.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

TagId World::RegisterTag(std::type_index type, std::string_view name) {
    return registries_ == nullptr ? 0 : registries_->Tags().Register(world_, type, name);
}

TagId World::FindTag(std::type_index type) const noexcept {
    return registries_ == nullptr ? 0 : registries_->Tags().Find(type);
}

void World::AddTag(Entity entity, TagId tag) noexcept {
    TagStorage::Add(world_, entity, tag);
}

bool World::HasTag(Entity entity, TagId tag) const noexcept {
    return TagStorage::Has(world_, entity, tag);
}

void World::RemoveTag(Entity entity, TagId tag) noexcept {
    TagStorage::Remove(world_, entity, tag);
}

} // namespace kb::ecs
