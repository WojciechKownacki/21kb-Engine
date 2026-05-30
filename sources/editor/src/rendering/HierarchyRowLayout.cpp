#include "rendering/HierarchyRowLayout.hpp"

#if defined(_WIN32)
#include "rendering/HierarchyPanelStyle.hpp"

namespace kb::editor {

HierarchyRowLayoutRects HierarchyRowLayout::Resolve(const RECT& rowRect, const EditorHierarchyRow& row) noexcept {
    const RECT visibilityCell{
        rowRect.left,
        rowRect.top,
        rowRect.left + HierarchyPanelStyle::EyeColumnWidth,
        rowRect.bottom,
    };
    const RECT visibilityIcon{
        visibilityCell.left + 5,
        visibilityCell.top + 5,
        visibilityCell.right - 5,
        visibilityCell.bottom - 5,
    };

    const int treeLeft = rowRect.left + HierarchyPanelStyle::EyeColumnWidth + 2;
    const int indent = treeLeft + static_cast<int>(row.depth) * HierarchyPanelStyle::IndentPerDepth;
    const RECT expanderHit{ indent - 2, rowRect.top, indent + 16, rowRect.bottom };
    const RECT expanderIcon{ indent + 3, rowRect.top + 7, indent + 11, rowRect.bottom - 7 };
    const RECT entityIcon{
        expanderIcon.right + 4,
        rowRect.top + 3,
        expanderIcon.right + 4 + HierarchyPanelStyle::RowIconSize,
        rowRect.top + 3 + HierarchyPanelStyle::RowIconSize,
    };

    return HierarchyRowLayoutRects{
        .visibilityCell = visibilityCell,
        .visibilityIcon = visibilityIcon,
        .expanderHit = expanderHit,
        .expanderIcon = expanderIcon,
        .entityIcon = entityIcon,
        .label = RECT{
            entityIcon.right + HierarchyPanelStyle::RowIconGap,
            rowRect.top + HierarchyPanelStyle::TextTopPadding,
            rowRect.right - 8,
            rowRect.bottom,
        },
    };
}

} // namespace kb::editor

#endif
