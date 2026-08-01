#pragma once

#include "rendering/HeroIconKind.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct InspectorComponentTile {
    std::string id;
    std::string category;
    std::string label;
    // Same icon source the dock tabs and Inspector section headers use
    // (HeroIconPainter), not a bespoke set — drawn beside the component name.
    HeroIconKind icon = HeroIconKind::Cube;
};

// One category in the Add Component menu's first level: its name and the icon
// shown beside it (also HeroIconPainter-drawn).
struct InspectorComponentCategory {
    std::string name;
    HeroIconKind icon = HeroIconKind::Cube;
};

class InspectorComponentCatalog {
public:
    InspectorComponentCatalog() = delete;

    [[nodiscard]] static std::span<const InspectorComponentTile> Tiles();
    // Distinct categories in display order, each with its icon.
    [[nodiscard]] static std::vector<InspectorComponentCategory> Categories();
    [[nodiscard]] static std::vector<const InspectorComponentTile*> Search(std::string_view query);
    // The tiles in one category, in display order.
    [[nodiscard]] static std::vector<const InspectorComponentTile*> InCategory(std::string_view category);
    [[nodiscard]] static const InspectorComponentTile* Find(std::string_view id);
    [[nodiscard]] static std::string_view RequiredPluginId(std::string_view componentId) noexcept;
};

} // namespace kb::editor
