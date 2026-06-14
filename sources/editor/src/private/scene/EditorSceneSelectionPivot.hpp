#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <optional>
#include <span>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class EditorSceneSelectionPivot {
public:
    EditorSceneSelectionPivot() = delete;

    [[nodiscard]] static std::optional<kb::scene::Vec3> Resolve(
        const kb::scene::Scene& scene,
        std::span<const kb::scene::SceneEntity> selected,
        kb::scene::SceneEntity fallback) noexcept;
};

} // namespace kb::editor
