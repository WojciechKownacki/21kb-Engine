#include "ecs/reflection/ComponentReflectionRegistry.hpp"

#include "ecs/reflection/ComponentReflectionFactory.hpp"
#include "ecs/reflection/ComponentReflectionValidator.hpp"

namespace kb::ecs {

const ComponentReflection* ComponentReflectionRegistry::Register(
    ComponentId componentId,
    std::string_view componentName,
    std::size_t componentSize,
    std::initializer_list<ComponentFieldDesc> fields) {
    if (const auto it = reflections_.find(componentId); it != reflections_.end()) {
        return &it->second;
    }

    const std::span<const ComponentFieldDesc> fieldSpan{ fields.begin(), fields.size() };
    if (!ComponentReflectionValidator::Validate(componentId, componentSize, fieldSpan)) {
        return nullptr;
    }

    auto [it, inserted] = reflections_.emplace(componentId, ComponentReflectionFactory::Create(componentId, componentName, componentSize, fieldSpan));
    static_cast<void>(inserted);
    return &it->second;
}

const ComponentReflection* ComponentReflectionRegistry::Find(ComponentId componentId) const noexcept {
    const auto it = reflections_.find(componentId);
    return it == reflections_.end() ? nullptr : &it->second;
}

const ComponentReflection* ComponentReflectionRegistry::Find(std::string_view componentName) const noexcept {
    for (const auto& [componentId, reflection] : reflections_) {
        static_cast<void>(componentId);
        if (reflection.Name() == componentName) {
            return &reflection;
        }
    }
    return nullptr;
}

void ComponentReflectionRegistry::Clear() noexcept {
    reflections_.clear();
}

} // namespace kb::ecs
