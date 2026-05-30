#include "ecs/snapshot/ComponentSnapshotCapture.hpp"

#include "ecs/snapshot/SnapshotEntityIndex.hpp"

#include <flecs.h>

#include <cstdint>
#include <cstring>

namespace kb::ecs {

void ComponentSnapshotCapture::Capture(ecs_world_t* world, const ComponentTypeInfo& componentType, SnapshotEntityIndex& entities) {
    if (world == nullptr || componentType.id == 0 || componentType.size == 0) {
        return;
    }

    ecs_iter_t iterator = ecs_each_id(world, componentType.id);
    while (ecs_each_next(&iterator)) {
        const void* components = ecs_field_w_size(&iterator, static_cast<ecs_size_t>(componentType.size), 0);
        if (components == nullptr) {
            continue;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(components);
        for (int32_t row = 0; row < iterator.count; ++row) {
            EntitySnapshot& entitySnapshot = entities.FindOrAdd(world, Entity{ static_cast<Entity::IdType>(iterator.entities[row]) });
            ComponentSnapshot& componentSnapshot = entitySnapshot.components.emplace_back();
            componentSnapshot.componentId = componentType.id;
            componentSnapshot.componentName = componentType.name;
            componentSnapshot.data.resize(componentType.size);
            std::memcpy(componentSnapshot.data.data(), bytes + (static_cast<std::size_t>(row) * componentType.size), componentType.size);
        }
    }
}

} // namespace kb::ecs
