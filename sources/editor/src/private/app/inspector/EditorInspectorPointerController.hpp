#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)
class EditorInspectorPointerController {
public:
    explicit EditorInspectorPointerController(EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y);
    [[nodiscard]] bool HandlePointerDrag(int x, int y);
    [[nodiscard]] bool HandlePointerUp();
    [[nodiscard]] bool HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta);
    [[nodiscard]] bool HandleDoubleClick(const RECT& content, int x, int y);
    [[nodiscard]] bool UpdateHover(const RECT& content, int x, int y);
    [[nodiscard]] bool UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y);
    [[nodiscard]] bool Contains(const std::optional<RECT>& content, int x, int y) const noexcept;
    [[nodiscard]] bool ClearHoverIfNeeded();
    [[nodiscard]] bool ShouldCaptureMouse() const noexcept;

private:
    EditorSceneContext& sceneContext_;
    bool shouldCaptureMouse_ = false;
};
#endif

} // namespace kb::editor
