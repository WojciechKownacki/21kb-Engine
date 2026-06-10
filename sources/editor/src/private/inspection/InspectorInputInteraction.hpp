#pragma once

#include "rendering/InspectorPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// Handles pointer clicks and key-capture for the input-related inspector
// sections (Input Action asset, Input Mapping Context asset, InputComponent on
// an entity). Pure dispatch over the EditorSceneContext authoring API — no
// rendering or geometry. Single responsibility: input inspector interaction.
class InspectorInputInteraction {
public:
#if defined(_WIN32)
    InspectorInputInteraction() = delete;

    [[nodiscard]] static bool HandleActionAssetClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit);
    [[nodiscard]] static bool HandleMappingClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit);
    [[nodiscard]] static bool HandleComponentClick(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, const InspectorPanelRenderer::Hit& hit);
    // Consumes the next key/mouse button while in key-capture mode. Returns true
    // if capture was active (and thus the event was handled).
    [[nodiscard]] static bool HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey);
#endif
};

} // namespace kb::editor
