#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class Scene;

class SceneEntityDestructionService {
public:
    SceneEntityDestructionService() = delete;

    static void DestroyObject(Scene& scene, SceneObject object) noexcept;
    static void DestroyEntity(Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
