#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;

class EditorAssetBrowserBreadcrumbHitTester {
public:
    EditorAssetBrowserBreadcrumbHitTester() = delete;

    [[nodiscard]] static std::optional<std::filesystem::path> FolderAt(
        const EditorAssetBrowserLayoutRects& layout,
        int x,
        int y,
        const EditorAssetBrowserState& state);
};

#endif

} // namespace kb::editor
