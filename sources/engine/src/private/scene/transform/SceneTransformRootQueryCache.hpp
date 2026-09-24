#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

namespace kb::scene {

struct SceneTransformRootQueryCache {
    kb::ecs::Query<TransformComponent> query;
    kb::ecs::UnsafeHotQuery<TransformComponent> hotQuery;
    std::uint64_t hierarchyTopologyVersion = 0U;
};

} // namespace kb::scene
