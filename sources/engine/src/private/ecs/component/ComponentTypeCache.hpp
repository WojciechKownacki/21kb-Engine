#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <typeindex>
#include <unordered_map>

namespace kb::ecs {

class ComponentTypeCache {
public:
    [[nodiscard]] ComponentId Find(std::type_index type) const noexcept;
    void Store(std::type_index type, ComponentId componentId);
    void Clear() noexcept;

private:
    std::unordered_map<std::type_index, ComponentId> componentIds_;
};

} // namespace kb::ecs
