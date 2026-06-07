#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)
class EditorProjectFilesMouseWheelController {
public:
    explicit EditorProjectFilesMouseWheelController(EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta);

private:
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
