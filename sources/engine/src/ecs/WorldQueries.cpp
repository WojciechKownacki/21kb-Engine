#include "engine/ecs/World.hpp"

#include "ecs/QueryStateFactory.hpp"

#include <span>

namespace kb::ecs {

QueryState* World::CreateQueryState(const ComponentId* componentIds, const std::size_t* componentSizes, std::size_t componentCount) const {
    if (world_ == nullptr || componentIds == nullptr || componentSizes == nullptr || componentCount == 0) {
        return nullptr;
    }

    return QueryStateFactory::Create(
        world_,
        std::span<const ComponentId>{ componentIds, componentCount },
        std::span<const std::size_t>{ componentSizes, componentCount },
        config_);
}

} // namespace kb::ecs
