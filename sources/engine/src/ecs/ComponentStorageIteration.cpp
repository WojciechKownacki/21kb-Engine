#include "ecs/component/ComponentStorageIteration.hpp"

#include <flecs.h>

#include <cstdint>

namespace kb::ecs {

void ComponentStorageIteration::ForEach(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawConstComponentVisitor visitor, void* context) {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        return;
    }

    ecs_query_desc_t desc{};
    desc.terms[0].id = componentId;
    desc.cache_kind = EcsQueryCacheAuto;
    ecs_query_t* query = ecs_query_init(world, &desc);
    if (query == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
        const void* components = ecs_field_w_size(&it, static_cast<ecs_size_t>(componentSize), 0);
        if (components == nullptr) {
            continue;
        }

        const auto* bytes = static_cast<const std::uint8_t*>(components);
        for (int32_t i = 0; i < it.count; ++i) {
            visitor(Entity{ it.entities[i] }, bytes + static_cast<std::size_t>(i) * componentSize, context);
        }
    }

    ecs_query_fini(query);
}

void ComponentStorageIteration::ForEachMutable(ecs_world_t* world, ComponentId componentId, std::size_t componentSize, RawMutableComponentVisitor visitor, void* context) {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        return;
    }

    ecs_query_desc_t desc{};
    desc.terms[0].id = componentId;
    desc.cache_kind = EcsQueryCacheAuto;
    ecs_query_t* query = ecs_query_init(world, &desc);
    if (query == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
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

    ecs_query_fini(query);
}

} // namespace kb::ecs
