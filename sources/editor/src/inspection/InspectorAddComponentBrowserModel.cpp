#include "inspection/InspectorAddComponentBrowserModel.hpp"

#include <algorithm>

namespace kb::editor {

std::vector<AddComponentRow> InspectorAddComponentBrowserModel::Rows(std::string_view category, std::string_view query) {
    std::vector<AddComponentRow> rows;
    if (!query.empty()) {
        for (const InspectorComponentTile* tile : InspectorComponentCatalog::Search(query)) {
            if (tile != nullptr) {
                rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Component, .label = tile->label, .icon = tile->icon, .id = tile->id });
            }
        }
        return rows;
    }
    if (!category.empty()) {
        for (const InspectorComponentTile* tile : InspectorComponentCatalog::InCategory(category)) {
            if (tile != nullptr) {
                rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Component, .label = tile->label, .icon = tile->icon, .id = tile->id });
            }
        }
        return rows;
    }
    for (const InspectorComponentCategory& entry : InspectorComponentCatalog::Categories()) {
        rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Category, .label = entry.name, .icon = entry.icon, .id = entry.name });
    }
    return rows;
}

int InspectorAddComponentBrowserModel::TotalHeight(int rowCount, int rowHeightPx) noexcept {
    return std::max(0, rowCount) * std::max(0, rowHeightPx);
}

int InspectorAddComponentBrowserModel::MaxScroll(int rowCount, int rowHeightPx, int listHeightPx) noexcept {
    return std::max(0, TotalHeight(rowCount, rowHeightPx) - std::max(0, listHeightPx));
}

InspectorAddComponentBrowserModel::VisibleWindow InspectorAddComponentBrowserModel::Visible(int rowCount, int scrollPx, int rowHeightPx, int listHeightPx) noexcept {
    if (rowCount <= 0 || rowHeightPx <= 0 || listHeightPx <= 0) {
        return {};
    }
    const int clampedScroll = std::clamp(scrollPx, 0, MaxScroll(rowCount, rowHeightPx, listHeightPx));
    const int first = clampedScroll / rowHeightPx;
    // +1 for the partially-scrolled top row, +1 for a partially-visible bottom row.
    const int visibleSpan = (listHeightPx + rowHeightPx - 1) / rowHeightPx + 1;
    const int count = std::min(rowCount - first, visibleSpan);
    return VisibleWindow{ .first = first, .count = std::max(0, count) };
}

} // namespace kb::editor
