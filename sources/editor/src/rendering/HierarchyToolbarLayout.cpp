#include "rendering/HierarchyToolbarLayout.hpp"

#if defined(_WIN32)
#include "rendering/HierarchyPanelStyle.hpp"

namespace kb::editor {

HierarchyToolbarLayoutRects HierarchyToolbarLayout::Resolve(const RECT& content) noexcept {
    const RECT header{ content.left, content.top, content.right, content.top + HierarchyPanelStyle::HeaderHeight };
    const RECT bottomLine{ header.left, header.bottom - 1, header.right, header.bottom };

    const int y = header.top + 7;
    const RECT addButton{
        header.left + HierarchyPanelStyle::HeaderPadLeft,
        y,
        header.left + HierarchyPanelStyle::HeaderPadLeft + HierarchyPanelStyle::ButtonWidth,
        y + HierarchyPanelStyle::SearchHeight,
    };
    const RECT searchBox{
        addButton.right + HierarchyPanelStyle::HeaderGap,
        y,
        header.right - HierarchyPanelStyle::HeaderPadRight - HierarchyPanelStyle::OptionsWidth,
        y + HierarchyPanelStyle::SearchHeight,
    };
    const RECT optionsButton{
        header.right - HierarchyPanelStyle::HeaderPadRight - HierarchyPanelStyle::OptionsWidth,
        y,
        header.right - HierarchyPanelStyle::HeaderPadRight,
        y + HierarchyPanelStyle::SearchHeight,
    };

    return HierarchyToolbarLayoutRects{
        .header = header,
        .bottomLine = bottomLine,
        .addButton = addButton,
        .searchBox = searchBox,
        .searchText = RECT{ searchBox.left + 24, searchBox.top + 3, searchBox.right - 6, searchBox.bottom },
        .searchIcon = RECT{ searchBox.left + 7, searchBox.top + 5, searchBox.left + 19, searchBox.top + 17 },
        .optionsButton = optionsButton,
        .listContent = RECT{ content.left, header.bottom, content.right, content.bottom },
    };
}

RECT HierarchyToolbarLayout::IconRect(const RECT& button) noexcept {
    return RECT{ button.left + 4, button.top + 3, button.right - 4, button.bottom - 3 };
}

} // namespace kb::editor

#endif
