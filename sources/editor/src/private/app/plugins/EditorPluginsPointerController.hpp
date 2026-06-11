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
class EditorPluginsPointerController {
public:
    explicit EditorPluginsPointerController(EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y);
    [[nodiscard]] bool HandlePointerMove(const std::optional<RECT>& content, int x, int y, bool leftButtonDown);
    [[nodiscard]] bool HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta);
    [[nodiscard]] bool UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y);
    [[nodiscard]] bool Contains(const std::optional<RECT>& content, int x, int y) const noexcept;

private:
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
