#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <string_view>

namespace kb::editor {

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
        std::wstring_view action);
};

#endif

} // namespace kb::editor
