#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "scene/transform_edit/EditorSceneTransformEditApplier.hpp"
#include "scene/transform_edit/EditorSceneTransformEditSession.hpp"

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

class EditorSceneTransformEditController {
public:
    EditorSceneTransformEditController(kb::scene::Scene& scene, EditorSceneTransformEditSession& session) noexcept;

    [[nodiscard]] EditorSceneTransformEditApplyResult ApplyPrimaryPosition(kb::scene::Vec3 position);
    [[nodiscard]] EditorSceneTransformEditApplyResult ApplyPrimaryRotation(kb::scene::Vec3 rotation);
    [[nodiscard]] EditorSceneTransformEditApplyResult ApplyRotationDelta(kb::scene::Quat delta);
    [[nodiscard]] EditorSceneTransformEditApplyResult ApplyPrimaryScale(kb::scene::Vec3 scale);
    [[nodiscard]] EditorSceneTransformEditApplyResult ApplyProperty(InspectorPropertyId property, float value);

    [[nodiscard]] static float PropertyStart(const EditorSceneTransformEditSession& session, InspectorPropertyId property) noexcept;

private:
    kb::scene::Scene& scene_;
    EditorSceneTransformEditSession& session_;
};

} // namespace kb::editor
