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
    [[nodiscard]] static std::optional<std::filesystem::path> SaveAs(HWND owner, const std::filesystem::path& initialPath, const std::filesystem::path& fallbackDirectory);
};

#endif

} // namespace kb::editor
