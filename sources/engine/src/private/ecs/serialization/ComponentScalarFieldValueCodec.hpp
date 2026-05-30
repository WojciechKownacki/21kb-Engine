#pragma once

#include "engine/ecs/ComponentFieldAccess.hpp"
#include "engine/ecs/ComponentSerialization.hpp"

#include <array>
#include <cstring>

namespace kb::ecs {

class ComponentScalarFieldValueCodec {
public:
    template <typename T>
    [[nodiscard]] static bool Read(
        const void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        ComponentFieldValue& output) noexcept {
        T value{};
        std::array<std::byte, sizeof(T)> bytes{};
        if (!ComponentFieldAccess::Read(component, componentSize, field, bytes)) {
            return false;
        }

        std::memcpy(&value, bytes.data(), sizeof(T));
        output = value;
        return true;
    }

    template <typename T>
    [[nodiscard]] static bool Write(
        void* component,
        std::size_t componentSize,
        const ComponentFieldReflection& field,
        const ComponentFieldValue& value) noexcept {
        const T* typedValue = std::get_if<T>(&value);
        if (typedValue == nullptr) {
            return false;
        }

        std::array<std::byte, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), typedValue, sizeof(T));
        return ComponentFieldAccess::Write(component, componentSize, field, bytes);
    }
};

} // namespace kb::ecs
