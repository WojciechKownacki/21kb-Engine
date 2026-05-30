#include "ecs/type/TagTypeRegistry.hpp"

namespace kb::ecs {

TagId TagTypeRegistry::Register(ecs_world_t* world, std::type_index type, std::string_view name) {
    return cache_.Register(world, type, name);
}

TagId TagTypeRegistry::Find(std::type_index type) const noexcept {
    return cache_.Find(type);
}

void TagTypeRegistry::Clear() noexcept {
    cache_.Clear();
}

} // namespace kb::ecs
