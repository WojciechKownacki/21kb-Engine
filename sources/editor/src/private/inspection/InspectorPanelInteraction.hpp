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
    [[nodiscard]] static bool HandleKeyDown(HWND owner, EditorSceneContext& sceneContext, WPARAM key);
    // Consumes the next key/mouse-button while the inspector is in key-capture mode
    // (mapping context "press a key" binding). Returns true if capture was active.
    [[nodiscard]] static bool HandleKeyCapture(EditorSceneContext& sceneContext, WPARAM virtualKey);
    [[nodiscard]] static bool UpdateHover(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept;
    [[nodiscard]] static bool HandleMouseWheel(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit, int wheelDelta) noexcept;
    [[nodiscard]] static bool HandleDoubleClick(EditorSceneContext& sceneContext, const InspectorPanelRenderer::Hit& hit) noexcept;
#endif
};

} // namespace kb::editor
