#pragma once

#include "engine/ui/UIComponentSet.hpp"

#include <cstdint>

namespace kb::scene {

// Calls `visit(std::uint64_t&)` for every entity reference a UI component set holds. Saving, prefab
// copies and editor undo remap these ids together, so a new reference field is added here once rather
// than to each of those paths.
template <typename Visit> void ForEachUIEntityReference(UIComponentSet& components, Visit&& visit) {
    if (components.selectable.has_value()) {
        visit(components.selectable->navigationUp);
        visit(components.selectable->navigationDown);
        visit(components.selectable->navigationLeft);
        visit(components.selectable->navigationRight);
        visit(components.selectable->targetGraphic);
    }
    if (components.toggle.has_value()) {
        visit(components.toggle->graphic);
    }
    if (components.scrollView.has_value()) {
        visit(components.scrollView->verticalScrollbar);
    }
    if (components.dropdown.has_value()) {
        visit(components.dropdown->templateEntity);
        visit(components.dropdown->captionText);
        visit(components.dropdown->captionImage);
        visit(components.dropdown->itemText);
        visit(components.dropdown->itemImage);
    }
}

} // namespace kb::scene
