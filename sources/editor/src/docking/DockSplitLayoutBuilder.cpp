#include "docking/DockSplitLayoutBuilder.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

DockSplitLayoutParts DockSplitLayoutBuilder::Build(const DockNode& node, const DockRect& rect, const DockLayoutBuildSettings& settings) const {
    const int splitter = settings.splitterSize;

    if (node.axis == DockSplitAxis::Horizontal) {
        const int firstWidth = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.width - splitter) * node.ratio)), 80, std::max(80, rect.width - splitter - 80));
        const DockRect splitterRect = DockGeometry::MakeRect(rect.x + firstWidth, rect.y, splitter, rect.height);
        return DockSplitLayoutParts{
            .first = DockGeometry::MakeRect(rect.x, rect.y, firstWidth, rect.height),
            .splitter = splitterRect,
            .second = DockGeometry::MakeRect(splitterRect.x + splitter, rect.y, rect.width - firstWidth - splitter, rect.height),
        };
    }

    const int firstHeight = DockGeometry::ClampInt(static_cast<int>(std::round(static_cast<float>(rect.height - splitter) * node.ratio)), 80, std::max(80, rect.height - splitter - 80));
    const DockRect splitterRect = DockGeometry::MakeRect(rect.x, rect.y + firstHeight, rect.width, splitter);
    return DockSplitLayoutParts{
        .first = DockGeometry::MakeRect(rect.x, rect.y, rect.width, firstHeight),
        .splitter = splitterRect,
        .second = DockGeometry::MakeRect(rect.x, splitterRect.y + splitter, rect.width, rect.height - firstHeight - splitter),
    };
}

} // namespace kb::editor
