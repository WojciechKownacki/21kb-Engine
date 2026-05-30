#include "ecs/ComponentStorage.hpp"

#include <flecs.h>

#include <cstdint>

namespace kb::ecs {

void ComponentStorage::ForEach(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world, componentId);
    while (ecs_each_next(&it)) {
        const void* components = ecs_field_w_size(&it, static_cast<ecs_size_t>(componentSize), 0);
        if (components == nullptr) {
            continue;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(components);
        for (int32_t i = 0; i < it.count; ++i) {
            visitor(Entity{ it.entities[i] }, bytes + static_cast<std::size_t>(i) * componentSize, context);
        }
    }
}

void ComponentStorage::ForEachMutable(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context) {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world, componentId);
    while (ecs_each_next(&it)) {
        void* components = ecs_field_w_size(&it, static_cast<ecs_size_t>(componentSize), 0);
        if (components == nullptr) {
            continue;
        }

        auto* bytes = static_cast<std::uint8_t*>(components);
        for (int32_t i = 0; i < it.count; ++i) {
            visitor(Entity{ it.entities[i] }, bytes + static_cast<std::size_t>(i) * componentSize, context);
            ecs_modified_id(world, it.entities[i], componentId);
        }
    }
}

} // namespace kb::ecs
