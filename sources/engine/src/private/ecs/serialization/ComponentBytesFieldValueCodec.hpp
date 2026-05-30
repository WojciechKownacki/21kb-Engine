#pragma once

#include "engine/ecs/ComponentFieldAccess.hpp"
#include "engine/ecs/ComponentSerialization.hpp"

#include <utility>
#include <vector>

namespace kb::ecs {

class ComponentBytesFieldValueCodec {
public:
    [[nodiscard]] static bool Read(
        const void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        ComponentFieldValue& output) noexcept {
        std::vector<std::byte> bytes(field.size);
        if (!ComponentFieldAccess::Read(component, componentSize, field, bytes)) {
            return false;
        }

        output = std::move(bytes);
        return true;
    }

    [[nodiscard]] static bool Write(
        void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        const ComponentFieldValue& value) noexcept {
        const auto* bytes = std::get_if<std::vector<std::byte>>(&value);
        if (bytes == nullptr) {
            return false;
        }
        return ComponentFieldAccess::Write(component, componentSize, field, *bytes);
    }
};

} // namespace kb::ecs
