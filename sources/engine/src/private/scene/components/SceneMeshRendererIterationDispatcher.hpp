#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneVisitors.hpp"

#include <cstdint>

namespace kb::scene {

class SceneMeshRendererIterationDispatcher {
public:
    SceneMeshRendererIterationDispatcher() = delete;

    static void ForEach(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t visibilityComponentId, std::uint64_t meshRendererComponentId, bool visibleOnly, MeshRendererVisitor visitor, void* context);
};

} // namespace kb::scene
