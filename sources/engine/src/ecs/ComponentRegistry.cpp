#include "ecs/ComponentRegistry.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

ComponentId ComponentRegistry::Register(ecs_world_t* world, std::type_index type, std::string_view name, std::size_t size, std::size_t alignment) {
    if (world == nullptr || size == 0 || alignment == 0) {
        return 0;
    }

    if (const auto it = componentIds_.find(type); it != componentIds_.end()) {
        return it->second;
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

    const ComponentId componentId = static_cast<ComponentId>(component);
    componentIds_.emplace(type, componentId);
    return componentId;
}

ComponentId ComponentRegistry::Find(std::type_index type) const noexcept {
    const auto it = componentIds_.find(type);
    return it == componentIds_.end() ? 0 : it->second;
}

void ComponentRegistry::Clear() noexcept {
    componentIds_.clear();
}

} // namespace kb::ecs
