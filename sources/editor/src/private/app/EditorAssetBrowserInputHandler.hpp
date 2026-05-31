#pragma once

#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorAssetBrowserInputHandler {
public:
#if defined(_WIN32)
    EditorAssetBrowserInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandleChar(HWND messageWindow, WPARAM wparam) const;
    [[nodiscard]] bool HandleKeyDown(HWND messageWindow, WPARAM wparam) const;
#endif

private:
#if defined(_WIN32)
    void Invalidate(HWND messageWindow) const;

    HWND mainWindow_ = nullptr;
    EditorSceneContext& sceneContext_;
#endif
};

} // namespace kb::editor
