#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)
class EditorProjectFilesDeleteConfirmOverlayController {
public:
    EditorProjectFilesDeleteConfirmOverlayController(HWND owner, EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandlePointerDown(int x, int y) const;

private:
    HWND owner_ = nullptr;
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
