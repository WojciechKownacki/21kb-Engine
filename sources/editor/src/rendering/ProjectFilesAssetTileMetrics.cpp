#include "rendering/ProjectFilesAssetTileMetrics.hpp"

#if defined(_WIN32)
#include "rendering/ProjectFilesPanelDrawing.hpp"

#include <algorithm>

namespace kb::editor {

int ProjectFilesAssetTileMetrics::NamePointSize(const RECT& tile) noexcept {
    return std::clamp(ProjectFilesPanelDrawing::RectHeight(tile) / 10, 9, 12);
}

ProjectFilesAssetTileVisualLayout ProjectFilesAssetTileMetrics::ResolveVisualLayout(RECT tile) noexcept {
    const int width = ProjectFilesPanelDrawing::RectWidth(tile);
    const int height = ProjectFilesPanelDrawing::RectHeight(tile);
    const int maxIconSize = std::max(16, std::min(48, width - 32));
    const int iconSize = std::clamp((height * 42) / 100, std::min(30, maxIconSize), maxIconSize);
    const int paddingMax = std::max(1, std::min(9, width / 2));
    const int padding = std::clamp(width / 13, std::min(5, paddingMax), paddingMax);
    const int namePoint = NamePointSize(tile);
    const int nameHeight = (namePoint + 5) * 2;
    const int gap = std::clamp(height / 42, 2, 4);
    const int contentHeight = iconSize + gap + nameHeight;
    const int iconTop = tile.top + std::max(6, (height - contentHeight) / 2);
    RECT icon{ tile.left + width / 2 - iconSize / 2, iconTop, tile.left + width / 2 + iconSize / 2, iconTop + iconSize };
    RECT label{ tile.left + padding, icon.bottom + gap, tile.right - padding, icon.bottom + gap + nameHeight };
    if (label.bottom > tile.bottom - padding) {
        const int delta = tile.bottom - padding - label.bottom;
        OffsetRect(&label, 0, delta);
        OffsetRect(&icon, 0, delta);
    }
    return ProjectFilesAssetTileVisualLayout{ .icon = icon, .label = label };
}

} // namespace kb::editor

#endif
