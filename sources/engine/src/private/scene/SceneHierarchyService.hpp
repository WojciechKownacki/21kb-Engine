#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyService {
public:
    SceneHierarchyService() = delete;

    [[nodiscard]] static SceneObject Parent(Scene& scene, SceneObject object);
    [[nodiscard]] static SceneEntity Parent(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::vector<SceneObject> Children(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneEntity> ChildEntities(const Scene& scene, SceneEntity entity);
    [[nodiscard]] static std::size_t ChildCount(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneEntity ChildAt(const Scene& scene, SceneEntity entity, std::size_t index) noexcept;
    [[nodiscard]] static std::vector<SceneObject> RootObjects(Scene& scene);
    [[nodiscard]] static std::vector<SceneEntity> RootEntities(const Scene& scene);
    [[nodiscard]] static bool SetParent(Scene& scene, SceneObject child, SceneObject parent) noexcept;
    [[nodiscard]] static bool SetParent(Scene& scene, SceneEntity child, SceneEntity parent) noexcept;
};

} // namespace kb::scene
