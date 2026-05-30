#include "engine/ecs/ComponentFieldAccess.hpp"

#include "ecs/reflection/ComponentFieldBounds.hpp"

#include <cstring>

namespace kb::ecs {

bool ComponentFieldAccess::Read(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::span<std::byte> output) noexcept {
    if (!CanAccess(component, componentSize, field, output.size())) {
        return false;
    }

    std::memcpy(output.data(), static_cast<const std::byte*>(component) + field.offset, field.size);
    return true;
}

bool ComponentFieldAccess::Write(void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::span<const std::byte> input) noexcept {
    if (!CanAccess(component, componentSize, field, input.size())) {
        return false;
    }

    std::memcpy(static_cast<std::byte*>(component) + field.offset, input.data(), field.size);
    return true;
}

bool ComponentFieldAccess::CanAccess(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, std::size_t bufferSize) noexcept {
    return component != nullptr && bufferSize >= field.size && ComponentFieldBounds::Contains(componentSize, field.offset, field.size);
}

} // namespace kb::ecs
