#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

class SceneEntityService {
public:
    SceneEntityService() = delete;

    [[nodiscard]] static SceneObject CreateObject(Scene& scene);
    [[nodiscard]] static SceneObject CreateObject(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene, SceneObjectDesc desc);
    static void DestroyObject(Scene& scene, SceneObject object) noexcept;
    static void DestroyEntity(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::string Name(const Scene& scene, SceneObject object);
    [[nodiscard]] static std::string Name(const Scene& scene, SceneEntity entity);
    static void SetName(Scene& scene, SceneObject object, std::string_view name);
    static void SetName(Scene& scene, SceneEntity entity, std::string_view name);
    [[nodiscard]] static std::size_t Count(const Scene& scene);
};

} // namespace kb::scene
