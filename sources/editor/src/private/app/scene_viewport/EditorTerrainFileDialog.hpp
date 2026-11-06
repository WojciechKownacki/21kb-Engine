#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string_view>

namespace kb::editor {

// The Open dialog for the greyscale images of the terrain tools (PNG, RAW, R16).
class EditorTerrainFileDialog final {
public:
    EditorTerrainFileDialog() = delete;
    [[nodiscard]] static std::optional<std::filesystem::path> PickHeightImage(HWND owner, std::string_view title);
};

} // namespace kb::editor
#endif
