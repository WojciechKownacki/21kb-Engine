#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::ecs {

enum class ComponentFieldType {
    Bool,
    Int32,
    UInt32,
    Float32,
    Float64,
    Vec2Float32,
    Vec3Float32,
    Vec4Float32,
    Enum32,
    Bytes
};

struct ComponentFieldDesc {
    std::string_view name;
    ComponentFieldType type = ComponentFieldType::Bytes;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct ComponentFieldReflection {
    std::string name;
    ComponentFieldType type = ComponentFieldType::Bytes;
    std::size_t offset = 0;
    std::size_t size = 0;
};

class ComponentReflection {
public:
    ComponentReflection() = default;
    ComponentReflection(ComponentId componentId, std::string componentName, std::size_t componentSize, std::vector<ComponentFieldReflection> fields);

    [[nodiscard]] ComponentId Id() const noexcept;
    [[nodiscard]] std::string_view Name() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] std::span<const ComponentFieldReflection> Fields() const noexcept;
    [[nodiscard]] const ComponentFieldReflection* FindField(std::string_view name) const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;

private:
    ComponentId componentId_ = 0;
    std::string componentName_;
    std::size_t componentSize_ = 0;
    std::vector<ComponentFieldReflection> fields_;
};

} // namespace kb::ecs
