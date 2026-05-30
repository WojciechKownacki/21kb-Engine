#include "ecs/reflection/ComponentReflectionValidator.hpp"

#include "ecs/reflection/ComponentFieldBounds.hpp"

namespace kb::ecs {

bool ComponentReflectionValidator::Validate(ComponentId componentId, std::size_t componentSize, std::span<const ComponentFieldDesc> fields) noexcept {
    if (componentId == 0 || componentSize == 0 || fields.empty()) {
        return false;
    }

    for (const ComponentFieldDesc& field : fields) {
        if (field.name.empty() || !IsFieldInBounds(componentSize, field)) {
            return false;
        }
    }
    return true;
}

bool ComponentReflectionValidator::IsFieldInBounds(std::size_t componentSize, const ComponentFieldDesc& field) noexcept {
    return ComponentFieldBounds::Contains(componentSize, field.offset, field.size);
}

} // namespace kb::ecs
