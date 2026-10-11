#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>

namespace kb::editor {

// What the user is about to do when the editor asks about unsaved scene changes.
enum class EditorSceneTransition {
    NewScene,
    OpenScene,
    CloseEditor,
    ConvertSceneToWorld,
    BuildGame,
};

#if defined(_WIN32)

enum class EditorSceneDirtyPromptResult {
    Save,
    DontSave,
    Cancel,
};

class EditorSceneDirtyPrompt {
public:
    EditorSceneDirtyPrompt() = delete;

    [[nodiscard]] static EditorSceneDirtyPromptResult Confirm(
        HWND owner,
        const std::filesystem::path& scenePath,
        EditorSceneTransition transition);
};

#endif

} // namespace kb::editor
