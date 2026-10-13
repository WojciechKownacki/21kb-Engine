#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "engine/assets/AssetImportTypes.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::editor {

// An import preset of the project (.kbimportpreset) the dialog offers: its name and options.
struct EditorAssetImportPreset {
    std::string name;
    kb::assets::AssetImportOptions options{};
};

#if defined(_WIN32)

struct EditorAssetImportSelection {
    std::vector<std::filesystem::path> files;
    kb::assets::AssetImportOptions options{};
};

class EditorAssetImportDialog {
public:
    EditorAssetImportDialog() = delete;

    [[nodiscard]] static std::optional<EditorAssetImportSelection> Open(HWND owner, std::span<const EditorAssetImportPreset> presets = {});
};

#endif

} // namespace kb::editor
