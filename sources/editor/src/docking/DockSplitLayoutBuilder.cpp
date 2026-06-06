#include "docking/DockSplitLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

DockSplitLayoutParts DockSplitLayoutBuilder::Build(const DockNode& node, const DockRect& rect, const DockLayoutBuildSettings& settings) const {
    const int splitter = std::max(1, settings.splitterSize);

    if (node.axis == DockSplitAxis::Horizontal) {
        const int firstWidth = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.width) * node.ratio)), 80, std::max(80, rect.width - 80));
        const int splitX = rect.x + firstWidth;
        const DockRect splitterRect = DockGeometry::MakeRect(splitX - splitter / 2, rect.y, splitter, rect.height);
        return DockSplitLayoutParts{
            .first = DockGeometry::MakeRect(rect.x, rect.y, firstWidth, rect.height),
            .splitter = splitterRect,
            .second = DockGeometry::MakeRect(splitX, rect.y, rect.width - firstWidth, rect.height),
        };
    }

    const int firstHeight = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.height) * node.ratio)), 80, std::max(80, rect.height - 80));
    const int splitY = rect.y + firstHeight;
    const DockRect splitterRect = DockGeometry::MakeRect(rect.x, splitY - splitter / 2, rect.width, splitter);
    return DockSplitLayoutParts{
        .first = DockGeometry::MakeRect(rect.x, rect.y, rect.width, firstHeight),
        .splitter = splitterRect,
        .second = DockGeometry::MakeRect(rect.x, splitY, rect.width, rect.height - firstHeight),
    };
}

} // namespace kb::editor
