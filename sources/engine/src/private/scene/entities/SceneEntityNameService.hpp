#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

class SceneEntityNameService {
public:
    SceneEntityNameService() = delete;

    [[nodiscard]] static std::string Name(const Scene& scene, SceneObject object);
    [[nodiscard]] static std::string Name(const Scene& scene, SceneEntity entity);
    static void SetName(Scene& scene, SceneObject object, std::string_view name);
    static void SetName(Scene& scene, SceneEntity entity, std::string_view name);
};

} // namespace kb::scene
