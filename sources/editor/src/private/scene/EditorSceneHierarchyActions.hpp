#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::editor {

class EditorSceneHierarchyActions {
public:
    EditorSceneHierarchyActions() = delete;

    [[nodiscard]] static bool ToggleVisibility(kb::scene::Scene& scene, kb::scene::SceneEntity entity);
    [[nodiscard]] static kb::scene::SceneEntity CreateObject(kb::scene::Scene& scene);
    [[nodiscard]] static bool Reparent(kb::scene::Scene& scene, kb::scene::SceneEntity child, kb::scene::SceneEntity parent);
};

} // namespace kb::editor
