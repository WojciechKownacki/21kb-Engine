#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"

#include <string>
#include <vector>

namespace kb::editor {

class EditorSceneTransformEditSession {
public:
    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] const std::string& LabelOrDefault() const noexcept;
    [[nodiscard]] kb::scene::SceneEntity Primary() const noexcept;
    [[nodiscard]] kb::scene::Vec3 TargetStart() const noexcept;
    [[nodiscard]] std::vector<EditorSceneObjectTransformChange>& Changes() noexcept;
    [[nodiscard]] const std::vector<EditorSceneObjectTransformChange>& Changes() const noexcept;
    [[nodiscard]] EditorSceneObjectTransformChange* PrimaryChange() noexcept;
    [[nodiscard]] const EditorSceneObjectTransformChange* PrimaryChange() const noexcept;

    void Begin(
        std::string label,
        kb::scene::SceneEntity primary,
        kb::scene::Vec3 targetStart,
        std::vector<EditorSceneObjectTransformChange> changes);
    void Clear() noexcept;

private:
    std::string label_;
    kb::scene::SceneEntity primary_{};
    kb::scene::Vec3 targetStart_{};
    std::vector<EditorSceneObjectTransformChange> changes_;
};

} // namespace kb::editor
