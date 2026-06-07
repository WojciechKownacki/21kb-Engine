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
class EditorConsolePointerController {
public:
    EditorConsolePointerController(HWND owner, EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandleContextMenu(const RECT& content, int x, int y);
    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y);
    [[nodiscard]] bool HandlePointerMove(const RECT& content, int y, bool leftButtonDown);
    [[nodiscard]] bool HandlePointerMove(const std::optional<RECT>& content, int y, bool leftButtonDown);
    [[nodiscard]] bool HandlePointerUp() noexcept;
    [[nodiscard]] bool HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta);
    [[nodiscard]] bool UpdateHover(const RECT& content, int x, int y);
    [[nodiscard]] bool UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y);

private:
    HWND owner_ = nullptr;
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
