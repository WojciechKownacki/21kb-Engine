#include "engine/ecs/ComponentReflection.hpp"

#include <utility>

namespace kb::ecs {

ComponentReflection::ComponentReflection(ComponentId componentId, std::string componentName, std::size_t componentSize, std::vector<ComponentFieldReflection> fields)
    : componentId_(componentId)
    , componentName_(std::move(componentName))
    , componentSize_(componentSize)
    , fields_(std::move(fields)) {}

ComponentId ComponentReflection::Id() const noexcept {
    return componentId_;
}

std::string_view ComponentReflection::Name() const noexcept {
    return componentName_;
}

std::size_t ComponentReflection::Size() const noexcept {
    return componentSize_;
}

std::span<const ComponentFieldReflection> ComponentReflection::Fields() const noexcept {
    return fields_;
}

const ComponentFieldReflection* ComponentReflection::FindField(std::string_view name) const noexcept {
    for (const ComponentFieldReflection& field : fields_) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

bool ComponentReflection::IsValid() const noexcept {
    return componentId_ != 0 && componentSize_ != 0;
}

} // namespace kb::ecs
