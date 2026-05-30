#include "ecs/component/ComponentTypeFactory.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

ComponentId ComponentTypeFactory::Create(ecs_world_t* world, std::string_view name, std::size_t size, std::size_t alignment) {
    if (world == nullptr || size == 0 || alignment == 0) {
        return 0;
    }

    ecs_component_desc_t desc{};
    desc.type.size = static_cast<ecs_size_t>(size);
    desc.type.alignment = static_cast<ecs_size_t>(alignment);

    const ecs_entity_t component = ecs_component_init(world, &desc);
    if (component == 0) {
        return 0;
    }

    if (!name.empty()) {
        const std::string ownedName{ name };
        ecs_set_name(world, component, ownedName.c_str());
    }

    return static_cast<ComponentId>(component);
}

} // namespace kb::ecs
