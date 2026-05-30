#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class Scene;

class SceneHierarchyParentAssignmentService {
public:
    SceneHierarchyParentAssignmentService() = delete;

    [[nodiscard]] static bool SetParent(Scene& scene, SceneObject child, SceneObject parent) noexcept;
    [[nodiscard]] static bool SetParent(Scene& scene, SceneEntity child, SceneEntity parent) noexcept;
};

} // namespace kb::scene
