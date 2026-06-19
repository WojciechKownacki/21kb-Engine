#pragma once

#include "ecs/component/ComponentTypeCache.hpp"
#include "ecs/component/ComponentTypeCatalog.hpp"
#include "engine/ecs/ComponentTypeInfo.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class ComponentRegistry {
public:
    [[nodiscard]] ComponentId Register(
        ecs_world_t* world,
        std::type_index type,
        std::string_view name,
        std::size_t size,
        std::size_t alignment,
        ComponentRegistrationOptions options);
    [[nodiscard]] ComponentId Find(std::type_index type) const noexcept;
    [[nodiscard]] const ComponentTypeInfo* FindInfo(ComponentId componentId) const noexcept;
    [[nodiscard]] std::span<const ComponentTypeInfo> Types() const noexcept;
    void Clear() noexcept;

private:
    ComponentTypeCache cache_;
    ComponentTypeCatalog catalog_;
};

} // namespace kb::ecs
