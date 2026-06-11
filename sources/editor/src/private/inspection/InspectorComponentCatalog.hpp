#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

struct InspectorComponentTile {
    std::string id;
    std::string category;
    std::string label;
};

class InspectorComponentCatalog {
public:
    InspectorComponentCatalog() = delete;

    [[nodiscard]] static std::span<const InspectorComponentTile> Tiles();
    [[nodiscard]] static std::vector<const InspectorComponentTile*> Search(std::string_view query);
    [[nodiscard]] static const InspectorComponentTile* Find(std::string_view id);
};

} // namespace kb::editor
