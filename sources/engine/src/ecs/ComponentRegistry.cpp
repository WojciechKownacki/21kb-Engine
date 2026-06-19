#include "ecs/ComponentRegistry.hpp"

#include "ecs/component/ComponentTypeFactory.hpp"

#include <string>

namespace kb::ecs {

ComponentId ComponentRegistry::Register(
    ecs_world_t* world,
    std::type_index type,
    std::string_view name,
    std::size_t size,
    std::size_t alignment,
    ComponentRegistrationOptions options) {
    if (const ComponentId cachedComponent = cache_.Find(type); cachedComponent != 0) {
        return cachedComponent;
    }

    const ComponentId componentId = ComponentTypeFactory::Create(world, name, size, alignment);
    if (componentId == 0) {
        return 0;
    }

    cache_.Store(type, componentId);
    catalog_.Add(ComponentTypeInfo{
        .id = componentId,
        .name = std::string{ name },
        .size = size,
        .alignment = alignment,
        .storageClass = options.storageClass,
    });
    return componentId;
}

ComponentId ComponentRegistry::Find(std::type_index type) const noexcept {
    return cache_.Find(type);
}

const ComponentTypeInfo* ComponentRegistry::FindInfo(ComponentId componentId) const noexcept {
    return catalog_.Find(componentId);
}

std::span<const ComponentTypeInfo> ComponentRegistry::Types() const noexcept {
    return catalog_.Types();
}

void ComponentRegistry::Clear() noexcept {
    cache_.Clear();
    catalog_.Clear();
}

} // namespace kb::ecs
