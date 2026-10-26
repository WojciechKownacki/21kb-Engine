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
        InspectorComponentTile{ .id = "Sprite2D", .category = "World", .label = "Sprite 2D", .icon = HeroIconKind::RectangleGroup },
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

struct ComponentNames {
    std::string_view id;
    std::string_view keywords;
    std::string_view description;
};

constexpr ComponentNames kComponentNames[] = {
    {"Camera", "view, viewport, eye, player view", "Shows the scene from this object: what the player sees."},
    {"Light", "lamp, sun, point light, spot light, shadow", "Lights the objects around it, and can cast shadows."},
    {"MeshRenderer", "mesh, model, 3d model, draw", "Draws a 3D model with a material at this object."},
    {"Particle Effect", "vfx, fx, smoke, fire, sparks, explosion, effect", "Plays a particle effect such as smoke, sparks or fire at this object."},
    {"TerrainEditor", "terrain, landscape, ground, heightmap, hills", "Sculpts and paints a landscape in the editor."},
    {"World2DMap", "tilemap, tile map, 2d map, tiles, level", "Holds a 2D tile map you paint with the Map Editor."},
    {"Sprite2D", "sprite, 2d sprite, animated sprite, picture, image, character art, flipbook, sprite sheet, atlas", "Draws a picture, or one cell of a sprite sheet, and plays a frame animation."},
    {"WorldBackdrop", "sky, skybox, background, horizon", "Draws the sky and distant background behind the scene."},
    {"Ambient Radiance", "ambient light, global illumination, gi, light probe, bounce light", "Adds soft indirect light that fills shadows around it."},
    {"Detail Switch", "lod, level of detail, distance, simplify", "Swaps to simpler models as the object gets far from the camera, to save speed."},
    {"Visibility Blocker", "occluder, occlusion, hide behind, wall culling", "Marks solid geometry that hides what is behind it, so hidden objects are not drawn."},
    {"Visibility Cell", "occlusion cell, room, pvs, visible set", "Defines a region that knows which objects can be seen from inside it."},
    {"Region Portal", "portal, doorway, room link, opening", "Marks an opening between two regions so the view can continue through it."},
    {"Secondary Frame", "render texture, second camera, mirror, minimap, security camera, picture in picture", "Renders a second view of the scene into a picture you can show on a surface or screen."},
    {"Geometry Swarm", "instancing, instanced mesh, foliage, grass, trees, scatter, crowd, many copies", "Draws thousands of copies of one model cheaply, for grass, trees or crowds."},
    {"Surface Cast", "decal, projected, stamp, bullet hole, stain, paint on surface", "Projects a picture onto the surfaces in front of it, like a stain or a mark."},
    {"Facing Panel", "billboard, sprite, camera facing, always faces camera, flat image, quad", "A flat picture that always turns to face the camera, like a tree sprite or a label in the world."},
    {"Line Renderer", "line, beam, rope, wire, laser, path line", "Draws a line or beam through a list of points."},
    {"Trail Renderer", "trail, ribbon, swoosh, streak, motion trail", "Leaves a fading ribbon behind a moving object."},
    {"Tags", "tag, layer, label, group, category, classification", "Gives the object names and layers so game logic and the editor can find it."},
    {"RegionShape", "zone, area, volume, trigger zone, bounds", "Describes an area of space other features can use."},
    {"GuideCurve", "spline, path, rail, curve, route", "A curve in space that other features can follow."},
    {"ContentInstance", "prefab link, linked scene, reference, sub scene", "Places another saved piece of content in this scene."},
    {"StreamFocus", "streaming, loader, load around, focus point", "Tells the world where to load content around."},
    {"AudioSource", "sound, speaker, music, sfx, play sound, audio clip", "Plays a sound from this object, in 3D space if you choose."},
    {"AudioListener", "ears, microphone, hearing, listener", "Marks where the player hears from."},
    {"Animator", "animation, animate, state machine, controller, character animation", "Plays and blends animations on a character through a state machine."},
    {"SkeletonBinding", "skeleton, bones, rig, skinned mesh", "Attaches a model to a skeleton so bones move it."},
    {"SocketAttachment", "socket, hold, attach to bone, weapon grip, hand", "Attaches this object to a named point on a character, such as a hand."},
    {"Combatant", "health, hp, team, damage, fighter, enemy, senses", "Gives the object health, a team and senses so it can fight and be hurt."},
    {"AIBrain", "ai, behaviour, behavior, enemy brain, rules, npc", "Lets the object decide what to do from simple rules, like chase or flee."},
    {"DeformedGeometry", "cloth, soft, wobble, bend, deform, jelly", "Bends and wobbles a model while it moves."},
    {"Rigidbody", "physics body, gravity, falls, pushable, simulation", "Makes the object move by physics: fall, bounce and be pushed."},
    {"Collider", "hitbox, collision, trigger, solid, blocks", "Gives the object a solid shape that things collide with, or a trigger area."},
    {"CharacterController", "player controller, walking, movement, character movement, capsule", "Moves a character through the world without falling through floors or walls."},
    {"Joint", "hinge, door, constraint, connect, spring, rope joint", "Connects two physics bodies, like a hinge or a spring."},
    {"NavAgent", "pathfinding, path, walk to, ai movement, navigation", "Lets the object walk to a point along a route that avoids obstacles."},
    {"NavObstacle", "blocker, avoid, obstacle, block path", "Makes walkers go around this object."},
    {"NavLink", "jump link, off mesh, ladder, connection, drop down", "Joins two walkable places that are not next to each other, like a jump or a ladder."},
};

[[nodiscard]] const ComponentNames* FindNames(std::string_view componentId) noexcept {
    for (const ComponentNames& names : kComponentNames) {
        if (names.id == componentId) {
            return &names;
        }
    }
    return nullptr;
}

// 0: no match; 1: the label, id or category names it; 2: only an everyday name (keyword) does. Search lists the
// first kind before the second, so typing a component's own name still puts that component first.
[[nodiscard]] int MatchRank(const InspectorComponentTile& tile, std::string_view query) {
    if (query.empty()) {
        return 1;
    }
    const std::string loweredQuery = LowerAscii(query);
    // The names the user reads match as well as the English ones the catalog keeps.
    if (LowerAscii(tile.label).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.category).find(loweredQuery) != std::string::npos ||
        LowerAscii(tile.id).find(loweredQuery) != std::string::npos ||
        LowerAscii(InspectorComponentCatalog::DisplayLabel(tile)).find(loweredQuery) != std::string::npos ||
        LowerAscii(InspectorComponentCatalog::CategoryLabel(tile.category)).find(loweredQuery) != std::string::npos) {
        return 1;
    }
    const ComponentNames* names = FindNames(tile.id);
    return names != nullptr && LowerAscii(names->keywords).find(loweredQuery) != std::string::npos ? 2 : 0;
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
    std::vector<const InspectorComponentTile*> byKeyword;
    const std::span<const InspectorComponentTile> tiles = Tiles();
    result.reserve(tiles.size());
    for (const InspectorComponentTile& tile : tiles) {
        const int rank = MatchRank(tile, query);
        if (rank == 1) {
            result.push_back(&tile);
        } else if (rank == 2) {
            byKeyword.push_back(&tile);
        }
    }
    result.insert(result.end(), byKeyword.begin(), byKeyword.end());
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
    if (tile.id == "Sprite2D") return std::string{ EditorText("component.sprite2D", "Sprite 2D") };
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
    if (category == "Gameplay") return EditorText("component.category.gameplay", "Gameplay");
    if (category == "Navigation") return EditorText("component.category.navigation", "Navigation");
    if (category == "Physics") return EditorText("component.category.physics", "Physics");
    if (category == "Rendering") return EditorText("component.category.rendering", "Rendering");
    if (category == "Scene") return EditorText("component.category.scene", "Scene");
    if (category == "User Widget") return EditorText("component.category.userWidget", "User Widget");
    if (category == "World") return EditorText("component.category.world", "World");
    return category;
}

std::string_view InspectorComponentCatalog::Keywords(std::string_view componentId) noexcept {
    const ComponentNames* names = FindNames(componentId);
    return names == nullptr ? std::string_view{} : names->keywords;
}

std::string_view InspectorComponentCatalog::Description(std::string_view componentId) noexcept {
    const ComponentNames* names = FindNames(componentId);
    return names == nullptr ? std::string_view{} : names->description;
}

std::string_view InspectorComponentCatalog::RequiredPluginId(std::string_view componentId) noexcept {
    if (componentId == "TerrainEditor") return "Editor.Terrain"; // editor-only component
    if (componentId == "World2DMap") return "World.Map2D";
    // Every scene component's plugin lives in the engine table the CLI uses too.
    const kb::scene::SceneComponentKind* kind = kb::scene::SceneComponentAuthoring::Find(componentId);
    return kind == nullptr ? std::string_view{} : kind->requiredPlugin;
}

} // namespace kb::editor
