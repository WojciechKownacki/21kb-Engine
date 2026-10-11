#pragma once

#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The File menu's scene commands, shared by the menu rows and their shortcuts so both do the same:
// New Scene... (Ctrl+N), Open Scene... (Ctrl+O) and Save As... (Ctrl+Shift+S). Each returns true when
// the scene changed and the view should be presented again.
class EditorSceneFileCommands {
public:
    EditorSceneFileCommands() = delete;

    [[nodiscard]] static bool NewScene(HWND owner, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool OpenScene(HWND owner, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool SaveSceneAs(HWND owner, EditorSceneContext& sceneContext);
    // Ctrl+N, Ctrl+O and Ctrl+Shift+S; false for any other key, which is left to the next handler.
    [[nodiscard]] static bool HandleShortcut(HWND owner, EditorSceneContext& sceneContext, WPARAM key, bool& changed);
};

#endif

} // namespace kb::editor
