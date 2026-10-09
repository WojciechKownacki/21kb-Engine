#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <span>
#include <vector>

namespace kb::scene {

class Scene;

class SceneEntityCreationService {
public:
    SceneEntityCreationService() = delete;

    [[nodiscard]] static SceneObject CreateObject(Scene& scene);
    [[nodiscard]] static SceneObject CreateObject(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene, SceneObjectDesc desc);
    // CreateObject for each description, with every entity created at once with its transform and visibility.
    [[nodiscard]] static std::vector<SceneObject> CreateObjects(Scene& scene, std::span<const SceneObjectDesc> descs,
        std::span<const kb::ecs::World::BulkComponentView> components = {});
};

} // namespace kb::scene
