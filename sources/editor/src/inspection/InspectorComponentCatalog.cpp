#include "inspection/InspectorComponentCatalog.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor {
namespace {

[[nodiscard]] std::vector<InspectorComponentTile> BuildTiles() {
    std::vector<InspectorComponentTile> tiles{
        InspectorComponentTile{ .id = "Camera", .category = "Rendering", .label = "Camera" },
        InspectorComponentTile{ .id = "Light", .category = "Rendering", .label = "Light" },
        InspectorComponentTile{ .id = "MeshRenderer", .category = "Rendering", .label = "Mesh Renderer" },
    };
    std::ranges::sort(tiles, [](const InspectorComponentTile& lhs, const InspectorComponentTile& rhs) {
        if (lhs.category != rhs.category) {
            return lhs.category < rhs.category;
        }
        return lhs.label < rhs.label;
    });
    return tiles;
}

[[nodiscard]] std::string LowerAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

[[nodiscard]] bool Matches(const InspectorComponentTile& tile, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    const std::string loweredQuery = LowerAscii(query);
    return LowerAscii(tile.label).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.category).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.id).find(loweredQuery) != std::string::npos;
}

} // namespace

std::span<const InspectorComponentTile> InspectorComponentCatalog::Tiles() {
    static const std::vector<InspectorComponentTile> kCachedTiles = BuildTiles();
    return kCachedTiles;
}

std::vector<const InspectorComponentTile*> InspectorComponentCatalog::Search(std::string_view query) {
    std::vector<const InspectorComponentTile*> result;
    const std::span<const InspectorComponentTile> tiles = Tiles();
    result.reserve(tiles.size());
    for (const InspectorComponentTile& tile : tiles) {
        if (Matches(tile, query)) {
            result.push_back(&tile);
        }
    }
    return result;
}

const InspectorComponentTile* InspectorComponentCatalog::Find(std::string_view id) {
    const std::span<const InspectorComponentTile> tiles = Tiles();
    const auto iter = std::ranges::find_if(tiles, [id](const InspectorComponentTile& tile) {
        return tile.id == id;
    });
    return iter == tiles.end() ? nullptr : &*iter;
}

} // namespace kb::editor
