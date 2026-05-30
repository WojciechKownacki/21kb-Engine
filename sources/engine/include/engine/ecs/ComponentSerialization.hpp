#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace kb::ecs {

using ComponentFieldValue = std::variant<
    std::monostate,
    bool,
    std::int32_t,
    std::uint32_t,
    float,
    double,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    std::vector<std::byte>>;

struct SerializedComponentField {
    std::string name;
    ComponentFieldType type = ComponentFieldType::Bytes;
    ComponentFieldValue value;
};

struct SerializedComponent {
    ComponentId componentId = 0;
    std::string componentName;
    std::vector<SerializedComponentField> fields;
};

class ComponentSerializer {
public:
    [[nodiscard]] static bool Serialize(const void* component, const ComponentReflection& reflection, SerializedComponent& output);
    [[nodiscard]] static bool Apply(const SerializedComponent& source, const ComponentReflection& reflection, void* component);
};

} // namespace kb::ecs
