#pragma once

#include "inspection/InspectorComponentCatalog.hpp"
#include "rendering/HeroIconKind.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

// One scrollable row in the Add Component menu — a category (first level) or a
// component (inside a category, or a search result). Excludes the fixed search
// box and the fixed "‹ Category" back header, which the renderer draws itself.
enum class AddComponentRowKind : std::uint8_t {
    Category,  // opens a category (shows a "›" chevron)
    Component, // adds the component
};

struct AddComponentRow {
    AddComponentRowKind kind = AddComponentRowKind::Category;
    std::string label;
    HeroIconKind icon = HeroIconKind::Cube;
    std::string id; // category name (Category) or component id (Component)
};

// Pure layout for the Unity-style Add Component menu: which rows are visible in
// the current view, plus the scroll/virtualization arithmetic. No editor state,
// so it is unit-testable against the catalog directly.
class InspectorAddComponentBrowserModel {
public:
    // The scrollable rows for the current view. A non-empty `query` overrides the
    // level and returns a flat list of matching components; otherwise a non-empty
    // `category` returns that category's components, and an empty one returns the
    // top-level category list.
    [[nodiscard]] static std::vector<AddComponentRow> Rows(std::string_view category, std::string_view query);

    [[nodiscard]] static int TotalHeight(int rowCount, int rowHeightPx) noexcept;
    // Largest valid scroll offset so the last row can reach the list bottom.
    [[nodiscard]] static int MaxScroll(int rowCount, int rowHeightPx, int listHeightPx) noexcept;

    // The window of rows intersecting [scrollPx, scrollPx + listHeightPx). Only
    // these need to be drawn/hit-tested (virtualization). `first` is clamped to a
    // valid index and `count` never runs past the row list.
    struct VisibleWindow {
        int first = 0;
        int count = 0;
    };
    [[nodiscard]] static VisibleWindow Visible(int rowCount, int scrollPx, int rowHeightPx, int listHeightPx) noexcept;
};

} // namespace kb::editor
