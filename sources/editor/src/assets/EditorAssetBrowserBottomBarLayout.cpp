#include "assets/EditorAssetBrowserBottomBarLayout.hpp"

#include "localization/EditorLocalization.hpp"
#include "rendering/gdi/EditorTextMeasure.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorAssetBrowserBottomBarLayout::Apply(EditorAssetBrowserLayoutRects& layout) noexcept {
    // The text buttons are as wide as their widest label in the editor's language, in the Project
    // Files body font (13 pt, ProjectFilesPanelRenderer) with 6 px each side.
    constexpr int labelPadding = 16;
    constexpr int bodyFontSize = 13;
    const int sortWidth = FitTextWidth(104, { EditorText("projectFiles.sortByName", "Sort: Name"),
        EditorText("projectFiles.sortByType", "Sort: Type"), EditorText("projectFiles.sortByPath", "Sort: Path") },
        bodyFontSize, FW_NORMAL, labelPadding);
    const int recursiveWidth = FitTextWidth(94, { EditorText("projectFiles.recursive", "Recursive"),
        EditorText("projectFiles.currentFolder", "Current") }, bodyFontSize, FW_NORMAL, labelPadding);
    layout.sortButton = RECT{ layout.bottomBar.left + 8, layout.bottomBar.top + 5, layout.bottomBar.left + 8 + sortWidth, layout.bottomBar.bottom - 5 };
    layout.sortMenu = RECT{ layout.sortButton.left, layout.sortButton.top - 78, layout.sortButton.right + 24, layout.sortButton.top - 4 };
    layout.sortNameItem = RECT{ layout.sortMenu.left, layout.sortMenu.top, layout.sortMenu.right, layout.sortMenu.top + 24 };
    layout.sortTypeItem = RECT{ layout.sortMenu.left, layout.sortNameItem.bottom, layout.sortMenu.right, layout.sortNameItem.bottom + 24 };
    layout.sortPathItem = RECT{ layout.sortMenu.left, layout.sortTypeItem.bottom, layout.sortMenu.right, layout.sortTypeItem.bottom + 24 };
    layout.listButton = RECT{ layout.sortButton.right + 8, layout.bottomBar.top + 5, layout.sortButton.right + 38, layout.bottomBar.bottom - 5 };
    layout.tileButton = RECT{ layout.listButton.right + 4, layout.bottomBar.top + 5, layout.listButton.right + 34, layout.bottomBar.bottom - 5 };
    layout.recursiveButton = RECT{ layout.tileButton.right + 8, layout.bottomBar.top + 5, layout.tileButton.right + 8 + recursiveWidth, layout.bottomBar.bottom - 5 };
    layout.sliderTrack = RECT{ layout.bottomBar.right - 170, layout.bottomBar.top + 14, layout.bottomBar.right - 18, layout.bottomBar.top + 18 };
    layout.sliderThumb = RECT{ layout.sliderTrack.left, layout.bottomBar.top + 8, layout.sliderTrack.left + 10, layout.bottomBar.bottom - 8 };
}

} // namespace kb::editor

#endif
