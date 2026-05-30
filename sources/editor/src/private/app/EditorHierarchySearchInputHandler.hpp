#pragma once

#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorHierarchySearchInputHandler {
public:
    EditorHierarchySearchInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept;

    [[nodiscard]] bool HandleChar(HWND messageWindow, WPARAM wparam) const;
    [[nodiscard]] bool HandleKeyDown(HWND messageWindow, WPARAM wparam) const;

private:
    void Invalidate(HWND messageWindow) const;

    HWND mainWindow_ = nullptr;
    EditorSceneContext& sceneContext_;
};

#endif

} // namespace kb::editor
