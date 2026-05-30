#pragma once

#include "ecs/type/TypeEntityCache.hpp"
#include "engine/ecs/TypeIds.hpp"

#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class TagTypeRegistry {
public:
    [[nodiscard]] TagId Register(ecs_world_t* world, std::type_index type, std::string_view name);
    [[nodiscard]] TagId Find(std::type_index type) const noexcept;
    void Clear() noexcept;

private:
    TypeEntityCache cache_;
};

} // namespace kb::ecs
