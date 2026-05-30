#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

namespace kb::scene {

class Scene;

class SceneEntityCreationService {
public:
    SceneEntityCreationService() = delete;

    [[nodiscard]] static SceneObject CreateObject(Scene& scene);
    [[nodiscard]] static SceneObject CreateObject(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene, SceneObjectDesc desc);
};

} // namespace kb::scene
