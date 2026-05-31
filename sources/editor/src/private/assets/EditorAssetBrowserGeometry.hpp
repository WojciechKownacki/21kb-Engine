#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserGeometry {
public:
    EditorAssetBrowserGeometry() = delete;

    [[nodiscard]] static bool Contains(const RECT& rect, int x, int y) noexcept;
    [[nodiscard]] static RECT DeleteConfirmRect(const RECT& bounds, int offsetX, int offsetY) noexcept;
    [[nodiscard]] static RECT DeleteConfirmAcceptRect(const RECT& dialog) noexcept;
    [[nodiscard]] static RECT DeleteConfirmCancelRect(const RECT& dialog) noexcept;
    [[nodiscard]] static RECT FolderDisclosureRect(RECT row, const EditorAssetFolderRow& folder) noexcept;
    [[nodiscard]] static RECT SliderHitRect(const EditorAssetBrowserLayoutRects& layout) noexcept;
    [[nodiscard]] static float SliderValueAt(const EditorAssetBrowserLayoutRects& layout, int x) noexcept;
    [[nodiscard]] static std::vector<std::string> BreadcrumbSegments(const std::filesystem::path& folder);
    [[nodiscard]] static std::string BreadcrumbDisplayLabel(const std::string& segment, std::size_t index);
    [[nodiscard]] static int BreadcrumbSegmentWidth(std::string_view label, bool root) noexcept;
};

#endif

} // namespace kb::editor
