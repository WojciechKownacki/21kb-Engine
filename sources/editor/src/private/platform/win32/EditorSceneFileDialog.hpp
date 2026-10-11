#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneFileDialog {
public:
    EditorSceneFileDialog() = delete;

    [[nodiscard]] static std::optional<std::filesystem::path> Open(HWND owner, const std::filesystem::path& initialDirectory);
    // The scene's new path inside `assetsFolder` (asked again for a place outside it), with the
    // .21kbscene extension appended to the name as typed.
    [[nodiscard]] static std::optional<std::filesystem::path> SaveAs(HWND owner, const std::filesystem::path& initialPath,
        const std::filesystem::path& fallbackDirectory, const std::filesystem::path& assetsFolder);
};

#endif

} // namespace kb::editor
