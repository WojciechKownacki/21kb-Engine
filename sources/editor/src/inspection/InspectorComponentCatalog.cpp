#include "inspection/InspectorComponentCatalog.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor {
namespace {

[[nodiscard]] std::vector<InspectorComponentTile> BuildTiles() {
    std::vector<InspectorComponentTile> tiles{
        InspectorComponentTile{ .id = "Camera", .category = "Rendering", .label = "Camera", .icon = HeroIconKind::Eye },
        InspectorComponentTile{ .id = "3D Radiance Emitter", .category = "Rendering", .label = "3D Radiance Emitter", .icon = HeroIconKind::Bolt },
        InspectorComponentTile{ .id = "MeshRenderer", .category = "Rendering", .label = "Mesh Renderer", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "TerrainEditor", .category = "World", .label = "Terrain Editor", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "WorldBackdrop", .category = "Rendering", .label = "World Backdrop", .icon = HeroIconKind::Eye },
        InspectorComponentTile{ .id = "Ambient Radiance", .category = "Rendering", .label = "Ambient Radiance", .icon = HeroIconKind::Bolt },
        InspectorComponentTile{ .id = "Detail Switch", .category = "Rendering", .label = "Detail Switch", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Visibility Blocker", .category = "Rendering", .label = "Visibility Blocker", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Visibility Cell", .category = "Rendering", .label = "Visibility Cell", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Region Portal", .category = "Rendering", .label = "Region Portal", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Secondary Frame", .category = "Rendering", .label = "Secondary Frame", .icon = HeroIconKind::Eye },
        InspectorComponentTile{ .id = "Geometry Swarm", .category = "Rendering", .label = "Geometry Swarm", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Surface Cast", .category = "Rendering", .label = "Surface Cast", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Facing Panel", .category = "Rendering", .label = "Facing Panel", .icon = HeroIconKind::Eye },
        InspectorComponentTile{ .id = "Kreska przestrzenna", .category = "Rendering", .label = "Kreska przestrzenna", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Wst\xC4\x99" "ga historii", .category = "Rendering", .label = "Wst\xC4\x99" "ga historii", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Tags", .category = "Scene", .label = "Object Classification", .icon = HeroIconKind::AdjustmentsHorizontal },
        InspectorComponentTile{ .id = "RegionShape", .category = "Scene", .label = "Region Shape", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "GuideCurve", .category = "Scene", .label = "Guide Curve", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "ContentInstance", .category = "Scene", .label = "Content Instance", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "StreamFocus", .category = "Scene", .label = "Stream Focus", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "AudioSource", .category = "Audio", .label = "Audio Source", .icon = HeroIconKind::SpeakerWave },
        InspectorComponentTile{ .id = "AudioListener", .category = "Audio", .label = "Audio Listener", .icon = HeroIconKind::SpeakerWave },
        InspectorComponentTile{ .id = "Animator", .category = "Animation", .label = "Animator", .icon = HeroIconKind::Play },
        InspectorComponentTile{ .id = "SkeletonBinding", .category = "Animation", .label = "Skeleton Binding", .icon = HeroIconKind::AdjustmentsHorizontal },
        InspectorComponentTile{ .id = "DeformedGeometry", .category = "Rendering", .label = "Deformed Geometry", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "UIDocument", .category = "UI", .label = "UI Document", .icon = HeroIconKind::DocumentText },
        InspectorComponentTile{ .id = "Rigidbody", .category = "Physics", .label = "Rigidbody", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Collider", .category = "Physics", .label = "Collider", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "CharacterController", .category = "Physics", .label = "Character Controller", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "Joint", .category = "Physics", .label = "Joint", .icon = HeroIconKind::RotationSnap },
        InspectorComponentTile{ .id = "NavAgent", .category = "Navigation", .label = "Nav Agent", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "NavObstacle", .category = "Navigation", .label = "Nav Obstacle", .icon = HeroIconKind::Cube },
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

std::vector<InspectorComponentCategory> InspectorComponentCatalog::Categories() {
    // A representative icon per category (HeroIconPainter, same source as tabs);
    // falls back to the first tile's icon for any category not listed here.
    const auto categoryIcon = [](std::string_view name, HeroIconKind fallback) {
        if (name == "Rendering") return HeroIconKind::Eye;
        if (name == "Physics") return HeroIconKind::Cube;
        if (name == "Audio") return HeroIconKind::SpeakerWave;
        if (name == "Animation") return HeroIconKind::Play;
        return fallback;
    };
    std::vector<InspectorComponentCategory> categories;
    for (const InspectorComponentTile& tile : Tiles()) {
        if (categories.empty() || categories.back().name != tile.category) {
            categories.push_back(InspectorComponentCategory{ .name = tile.category, .icon = categoryIcon(tile.category, tile.icon) });
        }
    }
    return categories;
}

std::vector<const InspectorComponentTile*> InspectorComponentCatalog::InCategory(std::string_view category) {
    std::vector<const InspectorComponentTile*> result;
    for (const InspectorComponentTile& tile : Tiles()) {
        if (tile.category == category) {
            result.push_back(&tile);
        }
    }
    return result;
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

std::string_view InspectorComponentCatalog::RequiredPluginId(std::string_view componentId) noexcept {
    if (componentId == "TerrainEditor") return "Editor.Terrain";
    if (componentId == "Rigidbody" || componentId == "Collider" ||
        componentId == "CharacterController" || componentId == "Joint") {
        return "Physics.Jolt";
    }
    if (componentId == "AudioSource" || componentId == "AudioListener") {
        return "Audio.Miniaudio";
    }
    if (componentId == "3D Radiance Emitter" || componentId == "Ambient Radiance") {
        return "Rendering.BasicLighting";
    }
    return {};
}

} // namespace kb::editor
