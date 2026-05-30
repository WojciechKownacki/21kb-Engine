#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"

#include <span>
#include <vector>

namespace kb::ecs {

class ComponentTypeCatalog {
public:
    void Add(ComponentTypeInfo typeInfo);
    [[nodiscard]] const ComponentTypeInfo* Find(ComponentId componentId) const noexcept;
    [[nodiscard]] std::span<const ComponentTypeInfo> Types() const noexcept;
    void Clear() noexcept;

private:
    std::vector<ComponentTypeInfo> componentTypes_;
};

} // namespace kb::ecs
