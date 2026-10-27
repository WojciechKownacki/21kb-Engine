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
    // What the menu shows: the tile's label and a category's name in the editor language. The tile keeps
    // its English label and category, which identify it.
    [[nodiscard]] static std::string DisplayLabel(const InspectorComponentTile& tile);
    [[nodiscard]] static std::string_view CategoryLabel(std::string_view category);
    [[nodiscard]] static std::string_view RequiredPluginId(std::string_view componentId) noexcept;
    // Everyday and technical names a component is also known by, comma separated and lower case (billboard for Facing
    // Panel, decal for Surface Cast), which Search matches next to the label. Empty when the label says it all.
    [[nodiscard]] static std::string_view Keywords(std::string_view componentId) noexcept;
    // One plain sentence, in English, on what the component does for the author; empty for a component without one.
    [[nodiscard]] static std::string_view Description(std::string_view componentId) noexcept;
};

} // namespace kb::editor
