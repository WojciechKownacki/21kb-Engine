#pragma once

#include "engine/ecs/ComponentFieldAccess.hpp"
#include "engine/ecs/ComponentSerialization.hpp"

#include <array>
#include <cstring>

namespace kb::ecs {

class ComponentFloatArrayFieldValueCodec {
public:
    template <std::size_t Count>
    [[nodiscard]] static bool Read(
        const void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        ComponentFieldValue& output) noexcept {
        std::array<float, Count> value{};
        std::array<std::byte, sizeof(float) * Count> bytes{};
        if (!ComponentFieldAccess::Read(component, componentSize, field, bytes)) {
            return false;
        }

        std::memcpy(value.data(), bytes.data(), bytes.size());
        output = value;
        return true;
    }

    template <std::size_t Count>
    [[nodiscard]] static bool Write(
        void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        const ComponentFieldValue& value) noexcept {
        const auto* typedValue = std::get_if<std::array<float, Count>>(&value);
        if (typedValue == nullptr) {
            return false;
        }

        std::array<std::byte, sizeof(float) * Count> bytes{};
        std::memcpy(bytes.data(), typedValue->data(), bytes.size());
        return ComponentFieldAccess::Write(component, componentSize, field, bytes);
    }
};

} // namespace kb::ecs
