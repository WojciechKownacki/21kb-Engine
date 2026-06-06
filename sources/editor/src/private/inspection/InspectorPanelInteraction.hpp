#pragma once

#include "rendering/InspectorPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class InspectorPanelInteraction {
public:
#if defined(_WIN32)
    InspectorPanelInteraction() = delete;

    [[nodiscard]] static bool HandlePointerDown(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int x, int y) noexcept;
    [[nodiscard]] static bool HandlePointerDrag(EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static bool HandlePointerUp(EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool HandleChar(EditorSceneContext& sceneContext, wchar_t character);
    [[nodiscard]] static bool HandleKeyDown(EditorSceneContext& sceneContext, WPARAM key);
    [[nodiscard]] static bool UpdateHover(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept;
#endif
};

} // namespace kb::editor
