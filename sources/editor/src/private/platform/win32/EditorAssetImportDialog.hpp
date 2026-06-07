#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetImportDialog {
public:
    EditorAssetImportDialog() = delete;

    [[nodiscard]] static std::vector<std::filesystem::path> Open(HWND owner);
};

#endif

} // namespace kb::editor
