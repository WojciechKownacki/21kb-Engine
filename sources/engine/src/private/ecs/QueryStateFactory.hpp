#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/WorldConfig.hpp"

#include <cstddef>
#include <span>

namespace kb::ecs {

class QueryState;
class NativeArchetypeStorage;

class QueryStateFactory {
public:
    [[nodiscard]] static QueryState* Create(
        NativeArchetypeStorage* nativeStorage,
        std::span<const ComponentId> componentIds,
        std::span<const std::size_t> componentSizes,
        std::span<const ComponentId> requiredComponentIds,
        std::span<const ComponentId> optionalComponentIds,
        std::span<const ComponentId> excludedComponentIds,
        std::span<const ComponentId> changedComponentIds,
        const WorldConfig& config);
};

} // namespace kb::ecs
