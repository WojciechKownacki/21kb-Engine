#include "inspection/InspectorComponentCatalog.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "localization/EditorLocalization.hpp"
#include "scene/ui/EditorUIComponentAuthoring.hpp"

#include <algorithm>
#include <cctype>

namespace kb::editor {
namespace {

[[nodiscard]] std::vector<InspectorComponentTile> BuildTiles() {
    std::vector<InspectorComponentTile> tiles{
        InspectorComponentTile{ .id = "Camera", .category = "Rendering", .label = "Camera", .icon = HeroIconKind::Eye },
        InspectorComponentTile{ .id = "Light", .category = "Rendering", .label = "Light", .icon = HeroIconKind::Bolt },
        InspectorComponentTile{ .id = "MeshRenderer", .category = "Rendering", .label = "Mesh Renderer", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Particle Effect", .category = "Rendering", .label = "Particle Effect", .icon = HeroIconKind::Bolt },
        InspectorComponentTile{ .id = "TerrainEditor", .category = "World", .label = "Terrain Editor", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "World2DMap", .category = "World", .label = "World 2D Map", .icon = HeroIconKind::RectangleGroup },
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
        InspectorComponentTile{ .id = "Line Renderer", .category = "Rendering", .label = "Line Renderer", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Trail Renderer", .category = "Rendering", .label = "Trail Renderer", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Tags", .category = "Scene", .label = "Object Classification", .icon = HeroIconKind::AdjustmentsHorizontal },
        InspectorComponentTile{ .id = "RegionShape", .category = "Scene", .label = "Region Shape", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "GuideCurve", .category = "Scene", .label = "Guide Curve", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "ContentInstance", .category = "Scene", .label = "Content Instance", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "StreamFocus", .category = "Scene", .label = "Stream Focus", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "AudioSource", .category = "Audio", .label = "Audio Source", .icon = HeroIconKind::SpeakerWave },
        InspectorComponentTile{ .id = "AudioListener", .category = "Audio", .label = "Audio Listener", .icon = HeroIconKind::SpeakerWave },
        InspectorComponentTile{ .id = "Animator", .category = "Animation", .label = "Animator", .icon = HeroIconKind::Play },
        InspectorComponentTile{ .id = "SkeletonBinding", .category = "Animation", .label = "Skeleton Binding", .icon = HeroIconKind::AdjustmentsHorizontal },
        InspectorComponentTile{ .id = "SocketAttachment", .category = "Animation", .label = "Socket Attachment", .icon = HeroIconKind::RotationSnap },
        InspectorComponentTile{ .id = "Combatant", .category = "Gameplay", .label = "Combatant", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "AIBrain", .category = "Gameplay", .label = "AI Brain", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "DeformedGeometry", .category = "Rendering", .label = "Deformed Geometry", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Rigidbody", .category = "Physics", .label = "Rigidbody", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "Collider", .category = "Physics", .label = "Collider", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "CharacterController", .category = "Physics", .label = "Character Controller", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "Joint", .category = "Physics", .label = "Joint", .icon = HeroIconKind::RotationSnap },
        InspectorComponentTile{ .id = "NavAgent", .category = "Navigation", .label = "Nav Agent", .icon = HeroIconKind::Gamepad2 },
        InspectorComponentTile{ .id = "NavObstacle", .category = "Navigation", .label = "Nav Obstacle", .icon = HeroIconKind::Cube },
        InspectorComponentTile{ .id = "NavLink", .category = "Navigation", .label = "Nav Link", .icon = HeroIconKind::RotationSnap },
    };
    for (const kb::scene::UIComponentDescriptor& definition : kb::scene::UIComponentCatalog()) {
        tiles.push_back(InspectorComponentTile{
            .id = std::string{ definition.stableId },
            .category = "User Widget",
            .label = std::string{ definition.displayName },
            .icon = definition.type == kb::scene::UIComponentType::Text ||
                    definition.type == kb::scene::UIComponentType::InputField
                ? HeroIconKind::DocumentText
                : HeroIconKind::RectangleGroup,
        });
    }
    for (const kb::scene::UIComponentPresetDescriptor& preset : kb::scene::UIComponentPresetCatalog()) {
        tiles.push_back(InspectorComponentTile{
            .id = std::string{ EditorUIPresetPrefix() } + std::string{ preset.name },
            .category = "User Widget",
            .label = std::string{ preset.name } + " Set",
            .icon = HeroIconKind::RectangleGroup,
        });
    }
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
    // The names the user reads match as well as the English ones the catalog keeps.
    return LowerAscii(tile.label).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.category).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.id).find(loweredQuery) != std::string::npos ||
        LowerAscii(InspectorComponentCatalog::DisplayLabel(tile)).find(loweredQuery) != std::string::npos ||
        LowerAscii(InspectorComponentCatalog::CategoryLabel(tile.category)).find(loweredQuery) != std::string::npos;
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
        if (name == "User Widget") return HeroIconKind::RectangleGroup;
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
    id = kb::scene::SceneComponentAuthoring::CanonicalName(id); // older names keep working
    const std::span<const InspectorComponentTile> tiles = Tiles();
    const auto iter = std::ranges::find_if(tiles, [id](const InspectorComponentTile& tile) {
        return tile.id == id;
    });
    return iter == tiles.end() ? nullptr : &*iter;
}

std::string InspectorComponentCatalog::DisplayLabel(const InspectorComponentTile& tile) {
    if (tile.id == "Camera") return std::string{ EditorText("component.camera", "Camera") };
    if (tile.id == "Light") return std::string{ EditorText("component.light", "Light") };
    if (tile.id == "MeshRenderer") return std::string{ EditorText("component.meshRenderer", "Mesh Renderer") };
    if (tile.id == "Particle Effect") return std::string{ EditorText("component.particleEffect", "Particle Effect") };
    if (tile.id == "TerrainEditor") return std::string{ EditorText("component.terrainEditor", "Terrain Editor") };
    if (tile.id == "World2DMap") return std::string{ EditorText("component.world2dMap", "World 2D Map") };
    if (tile.id == "WorldBackdrop") return std::string{ EditorText("component.worldBackdrop", "World Backdrop") };
    if (tile.id == "Ambient Radiance") return std::string{ EditorText("component.ambientRadiance", "Ambient Radiance") };
    if (tile.id == "Detail Switch") return std::string{ EditorText("component.detailSwitch", "Detail Switch") };
    if (tile.id == "Visibility Blocker") return std::string{ EditorText("component.visibilityBlocker", "Visibility Blocker") };
    if (tile.id == "Visibility Cell") return std::string{ EditorText("component.visibilityCell", "Visibility Cell") };
    if (tile.id == "Region Portal") return std::string{ EditorText("component.regionPortal", "Region Portal") };
    if (tile.id == "Secondary Frame") return std::string{ EditorText("component.secondaryFrame", "Secondary Frame") };
    if (tile.id == "Geometry Swarm") return std::string{ EditorText("component.geometrySwarm", "Geometry Swarm") };
    if (tile.id == "Surface Cast") return std::string{ EditorText("component.surfaceCast", "Surface Cast") };
    if (tile.id == "Facing Panel") return std::string{ EditorText("component.facingPanel", "Facing Panel") };
    if (tile.id == "Line Renderer") return std::string{ EditorText("component.lineRenderer", "Line Renderer") };
    if (tile.id == "Trail Renderer") return std::string{ EditorText("component.trailRenderer", "Trail Renderer") };
    if (tile.id == "Tags") return std::string{ EditorText("component.objectClassification", "Object Classification") };
    if (tile.id == "RegionShape") return std::string{ EditorText("component.regionShape", "Region Shape") };
    if (tile.id == "GuideCurve") return std::string{ EditorText("component.guideCurve", "Guide Curve") };
    if (tile.id == "ContentInstance") return std::string{ EditorText("component.contentInstance", "Content Instance") };
    if (tile.id == "StreamFocus") return std::string{ EditorText("component.streamFocus", "Stream Focus") };
    if (tile.id == "AudioSource") return std::string{ EditorText("component.audioSource", "Audio Source") };
    if (tile.id == "AudioListener") return std::string{ EditorText("component.audioListener", "Audio Listener") };
    if (tile.id == "Animator") return std::string{ EditorText("component.animator", "Animator") };
    if (tile.id == "SkeletonBinding") return std::string{ EditorText("component.skeletonBinding", "Skeleton Binding") };
    if (tile.id == "SocketAttachment") return std::string{ EditorText("component.socketAttachment", "Socket Attachment") };
    if (tile.id == "Combatant") return std::string{ EditorText("component.combatant", "Combatant") };
    if (tile.id == "AIBrain") return std::string{ EditorText("component.aiBrain", "AI Brain") };
    if (tile.id == "DeformedGeometry") return std::string{ EditorText("component.deformedGeometry", "Deformed Geometry") };
    if (tile.id == "Rigidbody") return std::string{ EditorText("component.rigidbody", "Rigidbody") };
    if (tile.id == "Collider") return std::string{ EditorText("component.collider", "Collider") };
    if (tile.id == "CharacterController") return std::string{ EditorText("component.characterController", "Character Controller") };
    if (tile.id == "Joint") return std::string{ EditorText("component.joint", "Joint") };
    if (tile.id == "NavAgent") return std::string{ EditorText("component.navAgent", "Nav Agent") };
    if (tile.id == "NavObstacle") return std::string{ EditorText("component.navObstacle", "Nav Obstacle") };
    if (tile.id == "NavLink") return std::string{ EditorText("component.navLink", "Nav Link") };
    for (const kb::scene::UIComponentDescriptor& definition : kb::scene::UIComponentCatalog()) {
        if (tile.id == definition.stableId) {
            return std::string{ InspectorUIComponentModel::DisplayName(definition.type) };
        }
    }
    for (const kb::scene::UIComponentPresetDescriptor& preset : kb::scene::UIComponentPresetCatalog()) {
        if (tile.id == std::string{ EditorUIPresetPrefix() } + std::string{ preset.name }) {
            return EditorLocalization::Format("component.uiPresetSet", "{preset} Set",
                { { "preset", std::string{ InspectorUIComponentModel::PresetDisplayName(preset.preset) } } });
        }
    }
    return tile.label;
}

std::string_view InspectorComponentCatalog::CategoryLabel(std::string_view category) {
    if (category == "Animation") return EditorText("component.category.animation", "Animation");
    if (category == "Audio") return EditorText("component.category.audio", "Audio");
    if (category == "Navigation") return EditorText("component.category.navigation", "Navigation");
    if (category == "Physics") return EditorText("component.category.physics", "Physics");
    if (category == "Rendering") return EditorText("component.category.rendering", "Rendering");
    if (category == "Scene") return EditorText("component.category.scene", "Scene");
    if (category == "User Widget") return EditorText("component.category.userWidget", "User Widget");
    if (category == "World") return EditorText("component.category.world", "World");
    return category;
}

std::string_view InspectorComponentCatalog::RequiredPluginId(std::string_view componentId) noexcept {
    if (componentId == "TerrainEditor") return "Editor.Terrain"; // editor-only component
    if (componentId == "World2DMap") return "World.Map2D";
    // Every scene component's plugin lives in the engine table the CLI uses too.
    const kb::scene::SceneComponentKind* kind = kb::scene::SceneComponentAuthoring::Find(componentId);
    return kind == nullptr ? std::string_view{} : kind->requiredPlugin;
}

} // namespace kb::editor
