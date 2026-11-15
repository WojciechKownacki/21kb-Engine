#pragma once

#include "app/EditorShellInteractionState.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <vector>

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
    // The Scene menu: the project's scenes, whether the open one can be duplicated, renamed or
    // deleted (a saved scene outside play mode and prefab editing), and those commands.
    [[nodiscard]] static std::vector<EditorPathMenuRow> SceneMenuRows(const EditorSceneContext& sceneContext);
    [[nodiscard]] static bool CanChangeOpenSceneFile(const EditorSceneContext& sceneContext);
    [[nodiscard]] static bool OpenSceneFile(HWND owner, EditorSceneContext& sceneContext, const std::filesystem::path& path);
    [[nodiscard]] static bool DuplicateScene(EditorSceneContext& sceneContext);
    [[nodiscard]] static bool RenameScene(HWND owner, EditorSceneContext& sceneContext);
    [[nodiscard]] static bool DeleteScene(HWND owner, EditorSceneContext& sceneContext);
    // Ctrl+N, Ctrl+O and Ctrl+Shift+S; false for any other key, which is left to the next handler.
    [[nodiscard]] static bool HandleShortcut(HWND owner, EditorSceneContext& sceneContext, WPARAM key, bool& changed);
};

#endif

} // namespace kb::editor
