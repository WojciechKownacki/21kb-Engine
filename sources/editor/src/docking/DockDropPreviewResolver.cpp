#include "docking/DockDropPreviewResolver.hpp"

#include "docking/DockGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kLeafEdgeBand = 0.40F;
constexpr int kOuterEdgeBand = 30;
constexpr int kStripMarkerHeight = 3;
constexpr float kRootSplitRatio = 0.25F;
constexpr float kLeafSplitRatio = 0.50F;

} // namespace

std::optional<DockDropPreview> DockDropPreviewResolver::Resolve(const DockLayout& layout, int x, int y) const {
    if (!layout.workspace.Contains(x, y)) {
        return std::nullopt;
    }

    for (auto it = layout.leaves.rbegin(); it != layout.leaves.rend(); ++it) {
        if (!it->frame.Contains(x, y)) {
            continue;
        }
        if (it->tabStrip.Contains(x, y)) {
            return DockDropPreview{
                .zone = DockDropZone::Center,
                .kind = DockDropPreviewKind::StripMarker,
                .leafId = it->leafId,
                .rect = DockGeometry::MakeRect(it->tabStrip.x, it->tabStrip.y + it->tabStrip.height, it->tabStrip.width, kStripMarkerHeight),
            };
        }
        if (it->content.Contains(x, y)) {
            const DockDropZone outerZone = DominantOuterEdge(layout.workspace, x, y);
            if (outerZone != DockDropZone::None) {
                return DockDropPreview{
                    .zone = outerZone,
                    .kind = DockDropPreviewKind::Glow,
                    .rect = DockGeometry::Split(layout.workspace, outerZone, kRootSplitRatio),
                };
            }

            const DockDropZone zone = ClassifyLeafZone(it->content, x, y);
            if (zone == DockDropZone::Center) {
                return std::nullopt;
            }
            return DockDropPreview{
                .zone = zone,
                .kind = DockDropPreviewKind::Glow,
                .leafId = it->leafId,
                .rect = DockGeometry::Split(it->content, zone, kLeafSplitRatio),
            };
        }
        return std::nullopt;
    }

    return DockDropPreview{ .zone = DockDropZone::Center, .kind = DockDropPreviewKind::Glow, .rect = layout.workspace };
}

DockDropZone DockDropPreviewResolver::DominantOuterEdge(const DockRect& rect, int x, int y) const {
    const bool nearLeft = x < rect.x + kOuterEdgeBand;
    const bool nearRight = x > rect.x + rect.width - kOuterEdgeBand;
    const bool nearTop = y < rect.y + kOuterEdgeBand;
    const bool nearBottom = y > rect.y + rect.height - kOuterEdgeBand;
    if (!nearLeft && !nearRight && !nearTop && !nearBottom) {
        return DockDropZone::None;
    }

    int bestDepth = nearLeft ? rect.x + kOuterEdgeBand - x : -1;
    DockDropZone bestZone = DockDropZone::Left;
    const int rightDepth = nearRight ? x - (rect.x + rect.width - kOuterEdgeBand) : -1;
    const int topDepth = nearTop ? rect.y + kOuterEdgeBand - y : -1;
    const int bottomDepth = nearBottom ? y - (rect.y + rect.height - kOuterEdgeBand) : -1;
    if (rightDepth > bestDepth) {
        bestDepth = rightDepth;
        bestZone = DockDropZone::Right;
    }
    if (topDepth > bestDepth) {
        bestDepth = topDepth;
        bestZone = DockDropZone::Top;
    }
    if (bottomDepth > bestDepth) {
        bestZone = DockDropZone::Bottom;
    }
    return bestZone;
}

DockDropZone DockDropPreviewResolver::ClassifyLeafZone(const DockRect& rect, int x, int y) const {
    const int band = static_cast<int>(std::round(static_cast<float>(std::min(rect.width, rect.height)) * kLeafEdgeBand));
    const bool inLeft = x < rect.x + band;
    const bool inRight = x > rect.x + rect.width - band;
    const bool inTop = y < rect.y + band;
    const bool inBottom = y > rect.y + rect.height - band;
    if (!inLeft && !inRight && !inTop && !inBottom) {
        return DockDropZone::Center;
    }

    int bestDepth = inLeft ? rect.x + band - x : -1;
    DockDropZone bestZone = DockDropZone::Left;
    const int rightDepth = inRight ? x - (rect.x + rect.width - band) : -1;
    const int topDepth = inTop ? rect.y + band - y : -1;
    const int bottomDepth = inBottom ? y - (rect.y + rect.height - band) : -1;
    if (rightDepth > bestDepth) {
        bestDepth = rightDepth;
        bestZone = DockDropZone::Right;
    }
    if (topDepth > bestDepth) {
        bestDepth = topDepth;
        bestZone = DockDropZone::Top;
    }
    if (bottomDepth > bestDepth) {
        bestZone = DockDropZone::Bottom;
    }
    return bestZone;
}

} // namespace kb::editor
