#include "rendering/ProjectFilesPanelRenderer.hpp"

#if defined(_WIN32)
#include "project/EditorProjectAssetIndex.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/GdiDrawing.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] RECT Row(RECT rect, int index, int height = 24) noexcept {
    rect.top += index * height;
    rect.bottom = rect.top + height;
    return rect;
}

void DrawLabel(HDC dc, RECT rect, const char* text, COLORREF color) {
    GdiDrawing::DrawTabText(dc, rect, text, color);
}

void DrawDynamicTree(HDC dc, RECT treeInner, const EditorTheme& theme) {
    DrawLabel(dc, Row(treeInner, 0), "Project", GdiDrawing::ToColorRef(theme.textPrimary));
    DrawLabel(dc, Row(treeInner, 2), "> Assets", GdiDrawing::ToColorRef(theme.textSecondary));

    const std::filesystem::path assetsRoot = EditorProjectPaths::AssetsRoot();
    std::filesystem::create_directories(EditorProjectPaths::PrefabsRoot());
    int row = 3;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(assetsRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string label = "  " + entry.path().filename().string();
        DrawLabel(dc, Row(treeInner, row++), label.c_str(), entry.path().filename() == "Prefabs" ? GdiDrawing::ToColorRef(theme.accent) : GdiDrawing::ToColorRef(theme.textSecondary));
    }
}

} // namespace

void ProjectFilesPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme) const {
    RECT frame = GdiDrawing::Inset(content, 8);
    const int treeWidth = (frame.right - frame.left) / 3;
    RECT tree = frame;
    tree.right = tree.left + treeWidth;
    RECT files = frame;
    files.left = tree.right + 8;

    GdiDrawing::DrawSharpFrame(dc, tree, GdiDrawing::ToColorRef(theme.strip), GdiDrawing::ToColorRef(theme.borderPanel));
    GdiDrawing::DrawSharpFrame(dc, files, GdiDrawing::ToColorRef(theme.panel), GdiDrawing::ToColorRef(theme.borderPanel));

    RECT treeInner = GdiDrawing::Inset(tree, 10);
    RECT filesInner = GdiDrawing::Inset(files, 10);
    DrawDynamicTree(dc, treeInner, theme);

    DrawLabel(dc, Row(filesInner, 0), "Assets / Prefabs", GdiDrawing::ToColorRef(theme.textPrimary));
    const std::vector<std::filesystem::path> prefabs = EditorProjectAssetIndex::PrefabAssets();
    for (std::size_t index = 0; index < prefabs.size(); ++index) {
        const std::string label = "[Prefab] " + prefabs[index].filename().string();
        DrawLabel(dc, Row(filesInner, static_cast<int>(index) + 2), label.c_str(), GdiDrawing::ToColorRef(theme.accent));
    }
}

} // namespace kb::editor

#endif
