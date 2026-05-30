#include "ecs/serialization/SerializedWorldEntityIndex.hpp"

namespace kb::ecs {

SerializedWorldEntityIndex::SerializedWorldEntityIndex(SerializedWorld& world) noexcept
    : world_(world) {}

SerializedEntity& SerializedWorldEntityIndex::Ensure(Entity::IdType sourceId, std::string_view name) {
    const auto [it, inserted] = indices_.try_emplace(sourceId, world_.entities.size());
    if (inserted) {
        world_.entities.push_back(SerializedEntity{
            .sourceId = sourceId,
            .name = std::string{ name },
            .parentSourceId = 0,
            .components = {},
        });
    }
    return world_.entities[it->second];
}

SerializedEntity* SerializedWorldEntityIndex::Find(Entity::IdType sourceId) noexcept {
    const auto it = indices_.find(sourceId);
    return it == indices_.end() ? nullptr : &world_.entities[it->second];
}

const SerializedEntity* SerializedWorldEntityIndex::Find(Entity::IdType sourceId) const noexcept {
    const auto it = indices_.find(sourceId);
    return it == indices_.end() ? nullptr : &world_.entities[it->second];
}

} // namespace kb::ecs
