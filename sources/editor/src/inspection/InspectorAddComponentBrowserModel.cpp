#include "inspection/InspectorAddComponentBrowserModel.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsAvailable(
    const InspectorComponentTile& tile,
    const InspectorAddComponentBrowserModel::PluginEnabledPredicate* pluginEnabled) {
    const std::string_view requiredPlugin =
        InspectorComponentCatalog::RequiredPluginId(tile.id);
    // Gameplay.AI and World.Map2D are on by default: their components are offered and adding one
    // switches the plugin on.
    return pluginEnabled == nullptr || requiredPlugin.empty() || requiredPlugin == "Gameplay.AI" ||
        requiredPlugin == "World.Map2D" || (*pluginEnabled)(requiredPlugin);
}

[[nodiscard]] std::vector<AddComponentRow> BuildRows(
    std::string_view category,
    std::string_view query,
    const InspectorAddComponentBrowserModel::PluginEnabledPredicate* pluginEnabled) {
    std::vector<AddComponentRow> rows;
    if (!query.empty()) {
        for (const InspectorComponentTile* tile : InspectorComponentCatalog::Search(query)) {
            if (tile != nullptr && IsAvailable(*tile, pluginEnabled)) {
                rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Component,
                    .label = InspectorComponentCatalog::DisplayLabel(*tile), .icon = tile->icon, .id = tile->id });
            }
        }
        return rows;
    }
    if (!category.empty()) {
        for (const InspectorComponentTile* tile : InspectorComponentCatalog::InCategory(category)) {
            if (tile != nullptr && IsAvailable(*tile, pluginEnabled)) {
                rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Component,
                    .label = InspectorComponentCatalog::DisplayLabel(*tile), .icon = tile->icon, .id = tile->id });
            }
        }
        // Alphabetical in the language shown; the catalog orders by the English labels.
        std::ranges::stable_sort(rows, {}, &AddComponentRow::label);
        return rows;
    }
    for (const InspectorComponentCategory& entry : InspectorComponentCatalog::Categories()) {
        const std::vector<const InspectorComponentTile*> tiles = InspectorComponentCatalog::InCategory(entry.name);
        if (std::ranges::any_of(tiles, [pluginEnabled](const InspectorComponentTile* tile) {
                return tile != nullptr && IsAvailable(*tile, pluginEnabled);
            })) {
            rows.push_back(AddComponentRow{ .kind = AddComponentRowKind::Category,
                .label = std::string{ InspectorComponentCatalog::CategoryLabel(entry.name) }, .icon = entry.icon, .id = entry.name });
        }
    }
    return rows;
}

} // namespace

std::vector<AddComponentRow> InspectorAddComponentBrowserModel::Rows(std::string_view category, std::string_view query) {
    return BuildRows(category, query, nullptr);
}

std::vector<AddComponentRow> InspectorAddComponentBrowserModel::Rows(
    std::string_view category,
    std::string_view query,
    const PluginEnabledPredicate& pluginEnabled) {
    return BuildRows(category, query, &pluginEnabled);
}

std::string_view InspectorAddComponentBrowserModel::Description(const std::vector<AddComponentRow>& rows, int index) noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) return {};
    const auto& row = rows[static_cast<std::size_t>(index)];
    return row.kind == AddComponentRowKind::Component ? InspectorComponentCatalog::Description(row.id) : std::string_view{};
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
