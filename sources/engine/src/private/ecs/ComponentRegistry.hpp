#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <cstddef>
#include <string_view>
#include <typeindex>
#include <unordered_map>

struct ecs_world_t;

namespace kb::ecs {

class ComponentRegistry {
public:
    [[nodiscard]] ComponentId Register(ecs_world_t* world, std::type_index type, std::string_view name, std::size_t size, std::size_t alignment);
    [[nodiscard]] ComponentId Find(std::type_index type) const noexcept;
    void Clear() noexcept;

private:
    std::unordered_map<std::type_index, ComponentId> componentIds_;
};

} // namespace kb::ecs
