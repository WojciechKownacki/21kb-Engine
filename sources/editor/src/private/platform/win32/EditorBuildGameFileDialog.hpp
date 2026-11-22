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
        HWND owner, const std::filesystem::path& initialDirectory, std::string_view title);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectPng(HWND owner);
    // A PNG or JPG picture, such as the loading screen.
    [[nodiscard]] static std::optional<std::filesystem::path> SelectImage(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectKeystore(HWND owner);
    // Where a new upload key is saved; `suggestedName` is offered in the dialog.
    [[nodiscard]] static std::optional<std::filesystem::path> SaveKeystore(HWND owner, std::string_view suggestedName);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectIdentity(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> SelectCertificate(HWND owner);
};
#endif

} // namespace kb::editor
