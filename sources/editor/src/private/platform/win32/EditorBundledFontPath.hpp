#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace kb::editor {

[[nodiscard]] inline std::filesystem::path EditorBundledFontPath() {
    std::wstring path(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path{path}.parent_path() / "Content" / "EditorShell" / "Fonts" / "DejaVuSans.ttf";
}

} // namespace kb::editor
