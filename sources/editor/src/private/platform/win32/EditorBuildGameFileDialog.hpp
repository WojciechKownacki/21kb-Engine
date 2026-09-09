#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <optional>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)
class EditorBuildGameFileDialog final {
public:
    EditorBuildGameFileDialog() = delete;
    [[nodiscard]] static std::optional<std::filesystem::path> SelectFolder(
        HWND owner, const std::filesystem::path& initialDirectory, std::wstring_view title);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectPng(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectKeystore(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectIdentity(HWND owner);
};
#endif

} // namespace kb::editor
