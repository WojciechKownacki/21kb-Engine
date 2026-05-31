#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

class SceneEntityService {
public:
    SceneEntityService() = delete;

    [[nodiscard]] static SceneObject CreateObject(Scene& scene);
    [[nodiscard]] static SceneObject CreateObject(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneObject DuplicateObject(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneObject> DuplicateObjects(Scene& scene, std::span<const SceneObject> objects);
    static void DestroyObject(Scene& scene, SceneObject object) noexcept;
    static void DestroyEntity(Scene& scene, SceneEntity entity) noexcept;
    static void DestroyObjects(Scene& scene, std::span<const SceneObject> objects) noexcept;
    [[nodiscard]] static bool SetParent(Scene& scene, std::span<const SceneObject> objects, SceneObject parent) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneObject Object(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::string Name(const Scene& scene, SceneObject object);
    [[nodiscard]] static std::string Name(const Scene& scene, SceneEntity entity);
    static void SetName(Scene& scene, SceneObject object, std::string_view name);
    static void SetName(Scene& scene, SceneEntity entity, std::string_view name);
    [[nodiscard]] static std::size_t Count(const Scene& scene);
};

} // namespace kb::scene
