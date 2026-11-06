#pragma once

#include "scene/EditorViewportCameraState.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::editor {

// The stage an asset editor shows its asset on: a camera, a floor, a key and a fill light, and a gradient
// sky with matching ambient light.
struct EditorPreviewStudio {
    kb::scene::SceneEntity camera{};
    kb::scene::SceneEntity floor{};
    kb::scene::SceneEntity environment{};
};

// Adds the stage to `scene`: the camera at `camera`'s pose and a square floor of side `2 * floorExtent` at
// height `floorHeight`.
[[nodiscard]] EditorPreviewStudio BuildEditorPreviewStudio(
    kb::scene::Scene& scene, const EditorViewportCameraState& camera, float floorHeight, float floorExtent);

// Moves the stage's camera entity to `camera`'s pose.
void SynchronizeEditorPreviewCamera(
    kb::scene::Scene& scene, kb::scene::SceneEntity cameraEntity, const EditorViewportCameraState& camera) noexcept;

} // namespace kb::editor
