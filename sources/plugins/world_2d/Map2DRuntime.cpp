#include "Map2DRuntime.hpp"
#include "Camera2D.hpp"
#include "Map2DView.hpp"
#include "Map2DEditing.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUI.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace kb::world2d {
namespace {
using namespace kb::assets;
using kb::scene::SceneEntity;
constexpr std::uint64_t BackgroundChunk = UINT64_MAX;
std::uint64_t ChunkKey(std::uint64_t tile) {
    return Map2DTileKey(Map2DTileKeyX(tile) / Map2DRuntime::ChunkSize,
        Map2DTileKeyY(tile) / Map2DRuntime::ChunkSize, Map2DTileKeyZ(tile));
}
Map2DAsset Header(const Map2DAsset& map) {
    auto result = MakeEmptyMap2DAsset(map.width, map.height);
    result.tileSize = map.tileSize;
    result.floorHeight = map.floorHeight;
    result.orientation = map.orientation;
    result.sheets = map.sheets;
    result.sortByY = map.sortByY;
    result.lit = map.lit;
    result.background = false;
    return result;
}
}

struct Map2DRuntime::State {
    struct Chunk {
        SceneEntity entity{};
        AssetId mesh{};
        std::uint64_t version = 0U;
        std::vector<Map2DSprite> animated;
        std::vector<std::uint32_t> frames;
        std::map<std::uint64_t, std::pair<std::string, SceneEntity>> prefabs;
        std::vector<AssetId> collisionAssets;
        SceneEntity shadowEntity{};
        AssetId shadowMesh{};
    };
    struct Instance {
        Map2DAsset map;
        std::uint64_t source = 0U;
        std::uint64_t hash = 0U;
        // A recreated map starts its animation at the beginning, including after
        // the editor restores its scene snapshot between Play sessions.
        double seconds = 0.0;
        std::map<std::uint64_t, std::map<std::uint64_t, Map2DTile>> cells;
        std::map<std::uint64_t, Chunk> chunks;
        std::set<std::uint64_t> dirty;
        std::uint64_t revision = 1U;
        kb::scene::TilemapRendererComponent renderer{};
        kb::scene::TilemapColliderComponent collider{};
        std::optional<kb::scene::MeshRendererComponent> originalRenderer;
        std::optional<kb::math::DVec3> cameraOrigin;
        // The play camera's centre in the map plane before it is snapped to whole pixels, so that slow smoothing
        // is not rounded away; empty until the camera has been driven.
        std::optional<std::array<float, 2>> cameraCentre;
        SceneEntity cameraTarget{};
        bool cameraStarted = false;
    };
    std::map<SceneEntity, Instance> instances;
    std::set<SceneEntity> camerasDriven;
    std::size_t rebuilt = 0U;

    static void Release(kb::scene::Scene& scene, Instance& instance) {
        for (const auto& [key, chunk] : instance.chunks) {
            static_cast<void>(key);
            if (scene.Entities().IsAlive(chunk.entity)) scene.Entities().Destroy(chunk.entity);
            static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(chunk.mesh));
            if (chunk.shadowMesh.IsValid()) static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(chunk.shadowMesh));
            for (const auto id : chunk.collisionAssets) static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(id));
        }
        instance.chunks.clear();
    }
};

namespace {

[[nodiscard]] SceneEntity FindNamedEntity(kb::scene::Scene& scene, SceneEntity from, std::string_view name) {
    if (scene.Entities().Name(from) == name) return from;
    for (const auto child : scene.Hierarchy().ChildEntities(from)) {
        if (const auto found = FindNamedEntity(scene, child, name); found.IsValid()) return found;
    }
    return {};
}

// Frames the map's play camera: a pixel perfect height from the viewport, and the follow behaviour of the
// map's camera settings. A map camera is driven once per frame however many layers of the map ask for it.
template <class Instance>
void DriveCamera(kb::scene::Scene& scene, SceneEntity owner, Instance& instance, float deltaSeconds, std::set<SceneEntity>& driven) {
    const Map2DCamera& settings = instance.map.camera;
    if (settings.pixelsPerUnit <= 0.0F && settings.followTarget.empty()) return;
    const auto query = std::as_const(scene).Components();
    const auto parent = scene.Hierarchy().Parent(owner);
    const SceneEntity container = query.Grids().Has(parent) && !query.Tilemaps().Has(parent) ? parent : owner;
    SceneEntity cameraEntity{};
    for (const auto child : scene.Hierarchy().ChildEntities(container)) {
        if (scene.Entities().Name(child) == kMap2DCameraName && query.Cameras().Has(child)) cameraEntity = child;
    }
    if (!cameraEntity.IsValid() || !driven.insert(cameraEntity).second) return;
    auto lens = *query.Cameras().TryGet(cameraEntity);
    if (lens.projection != kb::scene::CameraProjection::Orthographic) return;

    const bool wall = instance.map.orientation == Map2DOrientation::Wall;
    const float size = instance.map.tileSize;
    const float wanted = static_cast<float>(instance.map.viewHeight) * size;
    const auto viewport = scene.UI().Frame().viewportSize;
    const PixelPerfectView view = settings.pixelsPerUnit > 0.0F
        ? ComputePixelPerfectView(viewport.y, settings.pixelsPerUnit, wanted) : PixelPerfectView{ .orthographicHeight = wanted };
    if (view.pixelPerfect && lens.orthographicHeight != view.orthographicHeight) {
        lens.orthographicHeight = view.orthographicHeight;
        scene.Components().Cameras().Set(cameraEntity, lens);
    }
    const float halfHeight = lens.orthographicHeight * 0.5F;
    const float aspect = viewport.x > 0.0F && viewport.y > 0.0F ? viewport.x / viewport.y
        : static_cast<float>(instance.map.viewWidth) / static_cast<float>(std::max(1U, instance.map.viewHeight));

    auto transform = scene.Transforms().Get(cameraEntity);
    const auto origin = scene.Transforms().WorldTranslation(container);
    if (!instance.cameraCentre) {
        instance.cameraCentre = std::array<float, 2>{ transform.localPosition.x, wall ? transform.localPosition.y : transform.localPosition.z };
    }
    Camera2DFollow frame;
    frame.current = *instance.cameraCentre;
    frame.target = frame.current;
    frame.started = true;
    frame.halfWidth = halfHeight * aspect;
    frame.halfHeight = halfHeight;
    frame.deadZoneWidth = settings.deadZoneWidth * size;
    frame.deadZoneHeight = settings.deadZoneHeight * size;
    frame.smoothingSeconds = settings.smoothingSeconds;
    frame.deltaSeconds = deltaSeconds;
    frame.teleportDistance = static_cast<float>(std::max(instance.map.viewWidth, instance.map.viewHeight)) * size;
    frame.clamp = settings.clampToMap;
    const float width = static_cast<float>(instance.map.width) * size;
    const float height = static_cast<float>(instance.map.height) * size;
    frame.area = wall ? Camera2DArea{ -width * 0.5F, 0.0F, width * 0.5F, height } : Camera2DArea{ -width * 0.5F, -height * 0.5F, width * 0.5F, height * 0.5F };

    if (!settings.followTarget.empty()) {
        if (!scene.Entities().IsAlive(instance.cameraTarget) || scene.Entities().Name(instance.cameraTarget) != settings.followTarget) {
            instance.cameraTarget = {};
            for (const auto root : scene.Hierarchy().RootEntities()) {
                if (const auto found = FindNamedEntity(scene, root, settings.followTarget); found.IsValid()) { instance.cameraTarget = found; break; }
            }
        }
        if (instance.cameraTarget.IsValid()) {
            const auto position = scene.Transforms().WorldTranslation(instance.cameraTarget) - origin;
            frame.target = { static_cast<float>(position.x), static_cast<float>(wall ? position.y : position.z) };
            frame.started = instance.cameraStarted;
            frame.current = *instance.cameraCentre;
            instance.cameraCentre = StepCamera2DFollow(frame);
            instance.cameraStarted = true;
        }
    } else if (settings.clampToMap) {
        instance.cameraCentre = StepCamera2DFollow(frame);
    }
    auto centre = *instance.cameraCentre;
    if (view.pixelPerfect) { centre[0] = SnapToPixels(centre[0], view.unitsPerPixel); centre[1] = SnapToPixels(centre[1], view.unitsPerPixel); }
    if (transform.localPosition.x != centre[0] || (wall ? transform.localPosition.y : transform.localPosition.z) != centre[1]) {
        transform.localPosition.x = centre[0];
        (wall ? transform.localPosition.y : transform.localPosition.z) = centre[1];
        scene.Transforms().Set(cameraEntity, transform);
    }
}

} // namespace

Map2DRuntime::Map2DRuntime() : state_(std::make_unique<State>()) {}
Map2DRuntime::~Map2DRuntime() = default;
void Map2DRuntime::OnCreate(kb::scene::SceneSystemContext& context) {
    context.GetScene().Runtime().SetTilemapRuntime(this);
}

std::uint64_t Map2DRuntime::Revision(SceneEntity entity) const {
    const auto found = state_->instances.find(entity);
    return found == state_->instances.end() ? 0U : found->second.revision;
}

const Map2DAsset* Map2DRuntime::Map(SceneEntity entity) const {
    const auto found = state_->instances.find(entity);
    return found == state_->instances.end() ? nullptr : &found->second.map;
}

std::optional<Map2DTile> Map2DRuntime::Tile(SceneEntity entity, std::uint32_t x, std::uint32_t y, std::int32_t floor) const {
    const auto* map = Map(entity);
    if (map == nullptr || x >= map->width || y >= map->height || floor < Map2DAsset::MinimumFloor || floor > Map2DAsset::MaximumFloor) return {};
    const auto found = map->tiles.find(Map2DTileKey(x, y, floor));
    return found == map->tiles.end() ? std::nullopt : std::optional<Map2DTile>{ found->second };
}

bool Map2DRuntime::SetTile(SceneEntity entity, std::uint32_t x, std::uint32_t y, std::int32_t floor,
    const Map2DTile& tile, std::string* error) {
    const auto found = state_->instances.find(entity);
    if (found == state_->instances.end()) {
        if (error) *error = "The tilemap has not been initialized";
        return false;
    }
    auto& instance = found->second;
    if (x >= instance.map.width || y >= instance.map.height || floor < Map2DAsset::MinimumFloor || floor > Map2DAsset::MaximumFloor) {
        if (error) *error = "Tile coordinates are outside the map";
        return false;
    }
    // Validate one cell against the same sheet contract used by source loading.
    auto validation = Header(instance.map);
    const auto key = Map2DTileKey(x, y, floor);
    if (!tile.IsEmpty()) validation.tiles.emplace(key, tile);
    if (!IsMap2DAssetValid(validation, error)) return false;
    const auto previous = instance.map.tiles.find(key);
    if ((previous == instance.map.tiles.end() && tile.IsEmpty()) ||
        (previous != instance.map.tiles.end() && previous->second == tile)) return true;
    const auto chunk = ChunkKey(key);
    const auto previousFloor = instance.map.tiles.empty() ? 0 : std::min(0, Map2DTileKeyZ(instance.map.tiles.begin()->first));
    if (tile.IsEmpty()) {
        instance.map.tiles.erase(key);
        instance.cells[chunk].erase(key);
    } else {
        instance.map.tiles[key] = tile;
        instance.cells[chunk][key] = tile;
    }
    instance.dirty.insert(chunk);
    ++instance.revision;
    if (!instance.map.groundRules.empty()) {
        const auto rules = Map2DBrushLibrary::FromMap(instance.map);
        Map2DEditor editor(instance.map, rules, {});
        const auto area = editor.Clamp(std::int64_t{x} - 1, std::int64_t{y} - 1, std::int64_t{x} + 1, std::int64_t{y} + 1);
        editor.RefreshBorders(area, floor);
        for (auto row = area.y0; row <= area.y1; ++row) for (auto column = area.x0; column <= area.x1; ++column) {
            const auto neighbor = Map2DTileKey(column, row, floor);
            auto& cells = instance.cells[ChunkKey(neighbor)];
            const auto next = instance.map.tiles.find(neighbor);
            const auto old = cells.find(neighbor);
            if (next == instance.map.tiles.end()) {
                if (cells.erase(neighbor)) instance.dirty.insert(ChunkKey(neighbor));
            } else if (old == cells.end() || old->second != next->second) {
                cells[neighbor] = next->second;
                instance.dirty.insert(ChunkKey(neighbor));
            }
        }
    }
    const auto nextFloor = instance.map.tiles.empty() ? 0 : std::min(0, Map2DTileKeyZ(instance.map.tiles.begin()->first));
    if (instance.map.background && previousFloor != nextFloor) instance.dirty.insert(BackgroundChunk);
    if (error) error->clear();
    return true;
}

bool Map2DRuntime::EraseTile(SceneEntity entity, std::uint32_t x, std::uint32_t y, std::int32_t floor, std::string* error) {
    return SetTile(entity, x, y, floor, {}, error);
}

bool Map2DRuntime::PaintGround(SceneEntity entity, std::uint32_t x, std::uint32_t y, std::int32_t floor,
    std::string_view brush, std::string* error) {
    const auto* map = Map(entity);
    if (!map) { if (error) *error = "The tilemap has not been initialized"; return false; }
    const auto library = Map2DBrushLibrary::FromMap(*map);
    const auto* ground = library.Find(brush);
    const auto rule = std::ranges::find(map->groundRules, brush, &Map2DGroundRule::name);
    if (!ground || rule == map->groundRules.end()) { if (error) *error = "Unknown ground brush"; return false; }
    auto tile = Tile(entity, x, y, floor).value_or(Map2DTile{});
    tile.ground = Map2DSprite{ rule->sheet, ground->VariantFor(x, y, floor) };
    return SetTile(entity, x, y, floor, tile, error);
}

std::size_t Map2DRuntime::RebuiltChunksLastUpdate() const noexcept { return state_->rebuilt; }

void Map2DRuntime::OnUpdate(kb::scene::SceneSystemContext& context) { Refresh(context, true); }
void Map2DRuntime::OnFixedUpdate(kb::scene::SceneSystemContext& context) { Refresh(context, false); }
void Map2DRuntime::OnUpdateEnd(kb::scene::SceneSystemContext& context) {
    if (std::ranges::any_of(state_->instances, [](const auto& entry) { return !entry.second.dirty.empty(); })) Refresh(context, false);
}
void Map2DRuntime::Refresh(kb::scene::SceneSystemContext& context, bool advanceTime) {
    auto& scene = context.GetScene();
    if (advanceTime) {
        state_->rebuilt = 0U;
        state_->camerasDriven.clear();
    }
    if (!scene.Runtime().IsPlaying()) {
        if (!state_->instances.empty()) OnDestroy(context);
        return;
    }
    scene.Runtime().SetTilemapRuntime(this);
    // Older scenes store the map in a MeshRenderer. Upgrade after querying, outside ECS iteration.
    std::vector<SceneEntity> legacy;
    context.EcsWorld().CreateQuery<kb::scene::MeshRendererComponent>().ForEach(
        [](SceneEntity entity, const kb::scene::MeshRendererComponent&, void* output) {
            static_cast<std::vector<SceneEntity>*>(output)->push_back(entity);
        }, &legacy);
    for (const auto entity : legacy) {
        if (std::as_const(scene).Components().Tilemaps().Has(entity)) continue;
        const auto id = std::as_const(scene).Components().MeshRenderers().TryGet(entity)->meshAssetId;
        auto& manager = scene.Assets().Manager();
        const auto* meta = manager.Registry().Find(AssetId{ id });
        if (!meta || (meta->sourceExtension != kMap2DAssetExtension && meta->physicalPath.extension() != kMap2DAssetExtension)) continue;
        const auto copy = *meta;
        const AssetLoadRequest request{ .metadata = copy, .resolvedPath = copy.physicalPath, .runtimePack = manager.RuntimePack() };
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!request.ReadSourceBytes(bytes, error)) { context.ReportError(error); continue; }
        if (const auto map = Map2DAssetIO::Load(bytes, &error)) SetUpMap2DComponents(scene, entity, id, *map);
        else context.ReportError(error);
    }
    std::vector<SceneEntity> owners;
    context.EcsWorld().CreateQuery<kb::scene::TilemapComponent>().ForEach(
        [](SceneEntity entity, const kb::scene::TilemapComponent&, void* output) {
            static_cast<std::vector<SceneEntity>*>(output)->push_back(entity);
        }, &owners);
    const auto query = std::as_const(scene).Components();
    auto& assets = scene.Assets().Manager();
    struct CameraChoice { SceneEntity entity{}; int priority = INT_MIN; } camera;
    context.EcsWorld().CreateQuery<kb::scene::CameraComponent>().ForEach(
        [](SceneEntity entity, const kb::scene::CameraComponent& value, void* output) {
            auto& choice = *static_cast<CameraChoice*>(output);
            if (value.primary && (value.priority > choice.priority || !choice.entity.IsValid())) choice = {entity, value.priority};
        }, &camera);
    for (auto it = state_->instances.begin(); it != state_->instances.end();) {
        if (scene.Entities().IsAlive(it->first) && query.Tilemaps().Has(it->first)) { ++it; continue; }
        State::Release(scene, it->second);
        it = state_->instances.erase(it);
    }
    for (const auto owner : owners) {
        const auto source = query.Tilemaps().TryGet(owner)->mapAssetId;
        const auto* metadata = assets.Registry().Find(AssetId{ source });
        if (metadata == nullptr) {
            if (auto old = state_->instances.find(owner); old != state_->instances.end()) {
                State::Release(scene, old->second);
                state_->instances.erase(old);
            }
            continue;
        }
        auto& instance = state_->instances[owner];
        if (instance.source != source || instance.hash != metadata->contentHash) {
            const AssetMetadata copy = *metadata;
            std::vector<std::uint8_t> bytes;
            std::string error;
            const AssetLoadRequest request{ .metadata = copy, .resolvedPath = copy.physicalPath, .runtimePack = assets.RuntimePack() };
            if (!request.ReadSourceBytes(bytes, error)) { context.ReportError("Tilemap: " + error); continue; }
            auto loaded = Map2DAssetIO::Load(bytes, &error);
            if (!loaded) { context.ReportError("Tilemap: " + error); continue; }
            State::Release(scene, instance);
            instance.cells.clear();
            instance.dirty.clear();
            instance.cameraOrigin.reset();
            instance.cameraCentre.reset();
            instance.cameraStarted = false;
            instance.seconds = 0.0;
            instance.map = std::move(*loaded);
            ++instance.revision;
            instance.source = source;
            instance.hash = copy.contentHash;
            for (const auto& [key, tile] : instance.map.tiles) instance.cells[ChunkKey(key)].emplace(key, tile);
            for (const auto& [key, cells] : instance.cells) { static_cast<void>(cells); instance.dirty.insert(key); }
            if (instance.map.background) instance.dirty.insert(BackgroundChunk);
            if (const auto* renderer = query.MeshRenderers().TryGet(owner); renderer && renderer->meshAssetId == source)
                instance.originalRenderer = *renderer;
            auto empty = Header(instance.map);
            if (!SetUpMap2DCollision(scene, owner, empty, nullptr, &error)) { context.ReportError(error); continue; }
        }
        if (advanceTime) {
            instance.seconds += std::max(0.0F, context.DeltaSeconds());
            DriveCamera(scene, owner, instance, std::max(0.0F, context.DeltaSeconds()), state_->camerasDriven);
        }
        const auto* renderer = query.TilemapRenderers().TryGet(owner);
        const auto* collider = query.TilemapColliders().TryGet(owner);
        const auto renderSettings = renderer ? *renderer : kb::scene::TilemapRendererComponent{ .enabled = false };
        const auto collisionSettings = collider ? *collider : kb::scene::TilemapColliderComponent{ .enabled = false };
        const auto parent = scene.Hierarchy().Parent(owner);
        const auto* grid = query.Grids().TryGet(owner);
        if (!grid) grid = query.Grids().TryGet(parent);
        if (grid && kb::scene::IsGridComponentValid(*grid)) {
            const auto orientation = grid->wall ? Map2DOrientation::Wall : Map2DOrientation::Flat;
            if (instance.map.tileSize != grid->cellSize || instance.map.floorHeight != grid->floorHeight || instance.map.orientation != orientation) {
                instance.map.tileSize = grid->cellSize;
                instance.map.floorHeight = grid->floorHeight;
                instance.map.orientation = orientation;
                ++instance.revision;
                for (const auto& [key, chunk] : instance.chunks) { static_cast<void>(chunk); instance.dirty.insert(key); }
            }
        }
        if (instance.renderer != renderSettings || instance.collider != collisionSettings) {
            for (const auto& [key, chunk] : instance.chunks) { static_cast<void>(chunk); instance.dirty.insert(key); }
            instance.renderer = renderSettings;
            instance.collider = collisionSettings;
        }
        kb::scene::Vec3 parallax{};
        if (camera.entity.IsValid() && (instance.map.parallaxX != 1.0F || instance.map.parallaxY != 1.0F)) {
            const auto position = scene.Transforms().WorldTranslation(camera.entity);
            if (!instance.cameraOrigin) instance.cameraOrigin = position;
            const auto transform = scene.Transforms().Get(owner);
            const auto delta = kb::math::Rotate(kb::math::Inverse(transform.worldRotation), kb::math::ToVec3(position - *instance.cameraOrigin));
            if (std::abs(transform.worldScale.x) > 0.0001F) parallax.x = delta.x / transform.worldScale.x * (1.0F - instance.map.parallaxX);
            if (instance.map.orientation == Map2DOrientation::Wall) {
                if (std::abs(transform.worldScale.y) > 0.0001F) parallax.y = delta.y / transform.worldScale.y * (1.0F - instance.map.parallaxY);
            } else if (std::abs(transform.worldScale.z) > 0.0001F) parallax.z = delta.z / transform.worldScale.z * (1.0F - instance.map.parallaxY);
        }
        for (const auto& [key, chunk] : instance.chunks) {
            if (!assets.AcquireLoaded<kb::render::RenderMeshAssetData>(chunk.mesh).IsLoaded()) instance.dirty.insert(key);
            for (std::size_t i = 0; i < chunk.animated.size(); ++i) {
                if (Map2DAnimatedSprite(instance.map, chunk.animated[i], instance.seconds).cell != chunk.frames[i]) {
                    instance.dirty.insert(key);
                    break;
                }
            }
        }
        std::set<std::uint64_t> completed;
        for (const auto key : instance.dirty) {
            auto fragment = Header(instance.map);
            if (key == BackgroundChunk) {
                fragment.background = true;
                // Retain the lowest floor when building the background, without duplicating its art.
                if (!instance.map.tiles.empty()) fragment.tiles[instance.map.tiles.begin()->first].solid = true;
            }
            else fragment.tiles = instance.cells[key];
            if (!fragment.background && fragment.tiles.empty()) {
                if (const auto found = instance.chunks.find(key); found != instance.chunks.end()) {
                    if (scene.Entities().IsAlive(found->second.entity)) scene.Entities().Destroy(found->second.entity);
                    static_cast<void>(assets.ReleaseGeneratedAsset(found->second.mesh));
                    if (found->second.shadowMesh.IsValid()) static_cast<void>(assets.ReleaseGeneratedAsset(found->second.shadowMesh));
                    for (const auto id : found->second.collisionAssets) static_cast<void>(assets.ReleaseGeneratedAsset(id));
                    instance.chunks.erase(found);
                }
                instance.cells.erase(key);
                completed.insert(key);
                ++state_->rebuilt;
                continue;
            }
            auto& chunk = instance.chunks[key];
            if (!scene.Entities().IsAlive(chunk.entity)) {
                chunk.entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
                    .name = "Tilemap Chunk", .parent = scene.Entities().Object(owner),
                    .visibility = { .mode = kb::scene::VisibilityMode::Inherit } });
                chunk.mesh = MakeAssetId("tilemap-runtime:" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) +
                    ":" + std::to_string(owner.Id()) + ":" + std::to_string(key));
            }
            const float lift = 0.0F; // Layer ordering is inherited by transient render proxies.
            auto mesh = kb::render::RenderMap2DMeshBuilder::Build(fragment, lift, instance.seconds);
            if (!mesh) { context.ReportError("Tilemap chunk geometry could not be built"); continue; }
            const auto path = "/Runtime/Tilemap/" + std::to_string(chunk.mesh.value);
            if (!assets.RegisterGeneratedAsset(AssetMetadata{ .id = chunk.mesh, .type = "RenderMesh",
                    .virtualPath = path, .contentHash = ++chunk.version }) ||
                !assets.PublishRuntimeAsset(chunk.mesh, std::make_shared<kb::render::RenderMeshAssetData>(std::move(*mesh)))) {
                context.ReportError("Tilemap: " + assets.LastError()); continue;
            }
            scene.Components().MeshRenderers().Set(chunk.entity,
                kb::scene::MeshRendererComponent{ .meshAssetId = chunk.mesh.value, .castsShadow = false, .receivesShadow = true,
                    .transient = true, .previewHidden = !instance.renderer.enabled });
            if (auto shadow = kb::render::RenderMap2DMeshBuilder::BuildOccluders(fragment, lift, instance.seconds); shadow && instance.renderer.enabled) {
                if (!scene.Entities().IsAlive(chunk.shadowEntity)) chunk.shadowEntity = scene.Entities().CreateEntity({.name = "Tilemap Occlusion", .parent = scene.Entities().Object(chunk.entity)});
                chunk.shadowMesh = MakeAssetId(path + ":occlusion");
                if (!assets.RegisterGeneratedAsset({.id = chunk.shadowMesh, .type = "RenderMesh", .virtualPath = path + "/Occlusion", .contentHash = chunk.version}) ||
                    !assets.PublishRuntimeAsset(chunk.shadowMesh, std::make_shared<kb::render::RenderMeshAssetData>(std::move(*shadow)))) {
                    context.ReportError("Tilemap occlusion: " + assets.LastError()); continue;
                }
                scene.Components().MeshRenderers().Set(chunk.shadowEntity, {.meshAssetId = chunk.shadowMesh.value, .shadowOnly = true, .transient = true});
            } else {
                if (scene.Entities().IsAlive(chunk.shadowEntity)) scene.Entities().Destroy(chunk.shadowEntity);
                if (chunk.shadowMesh.IsValid()) static_cast<void>(assets.ReleaseGeneratedAsset(chunk.shadowMesh));
                chunk.shadowEntity = {}; chunk.shadowMesh = {};
            }
            chunk.animated.clear();
            chunk.frames.clear();
            const auto remember = [&](Map2DSprite sprite) {
                const auto* definition = Map2DDefinition(instance.map, sprite);
                if (!definition || definition->frames.size() < 2U) return;
                chunk.animated.push_back(sprite);
                chunk.frames.push_back(Map2DAnimatedSprite(instance.map, sprite, instance.seconds).cell);
            };
            for (const auto& [cell, tile] : fragment.tiles) {
                static_cast<void>(cell);
                remember(tile.ground);
                for (auto sprite : tile.borders) remember(sprite);
                for (auto sprite : tile.items) remember(sprite);
            }
            for (auto it = chunk.prefabs.begin(); it != chunk.prefabs.end();) {
                const auto cell = fragment.tiles.find(it->first);
                if (cell != fragment.tiles.end() && cell->second.prefab == it->second.first && scene.Entities().IsAlive(it->second.second)) { ++it; continue; }
                if (scene.Entities().IsAlive(it->second.second)) scene.Entities().Destroy(it->second.second);
                it = chunk.prefabs.erase(it);
            }
            for (const auto& [cell, tile] : fragment.tiles) {
                if (tile.prefab.empty() || chunk.prefabs.contains(cell)) continue;
                const auto prefab = scene.Assets().LoadPrefab(kb::assets::Utf8Path(tile.prefab));
                if (!prefab.IsLoaded()) { context.ReportError("Tilemap prefab could not be loaded: " + tile.prefab); continue; }
                const auto spawned = scene.Prefabs().Instantiate(*prefab.Get(), { .parent = scene.Entities().Object(chunk.entity) });
                if (spawned.Empty()) { context.ReportError("Tilemap prefab is empty: " + tile.prefab); continue; }
                const auto root = spawned.RootObject().Entity();
                auto transform = scene.Transforms().Get(root);
                const auto x = (static_cast<float>(Map2DTileKeyX(cell)) + 0.5F - static_cast<float>(fragment.width) * 0.5F) * fragment.tileSize;
                const auto y = static_cast<float>(Map2DTileKeyY(cell)) + 1.0F;
                const auto elevation = static_cast<float>(Map2DTileKeyZ(cell)) * fragment.floorHeight;
                const auto position = fragment.orientation == Map2DOrientation::Wall
                    ? kb::scene::Vec3{x, (static_cast<float>(fragment.height) - y) * fragment.tileSize, -elevation}
                    : kb::scene::Vec3{x, elevation, (static_cast<float>(fragment.height) * 0.5F - y) * fragment.tileSize};
                transform.localPosition = transform.localPosition + position;
                scene.Transforms().Set(root, transform);
                chunk.prefabs.emplace(cell, std::make_pair(tile.prefab, root));
            }
            if (!instance.collider.enabled || key == BackgroundChunk) fragment.tiles.clear();
            std::string error;
            std::vector<AssetId> collisionAssets;
            if (!SetUpMap2DCollision(scene, chunk.entity, fragment, nullptr, &error, &collisionAssets)) { context.ReportError(error); continue; }
            for (const auto id : chunk.collisionAssets) if (std::ranges::find(collisionAssets, id) == collisionAssets.end()) static_cast<void>(assets.ReleaseGeneratedAsset(id));
            chunk.collisionAssets = std::move(collisionAssets);
            completed.insert(key);
            ++state_->rebuilt;
        }
        for (auto key : completed) instance.dirty.erase(key);
        for (const auto& [key, chunk] : instance.chunks) {
            static_cast<void>(key);
            auto transform = scene.Transforms().Get(chunk.entity);
            if (transform.localPosition.x != parallax.x || transform.localPosition.y != parallax.y || transform.localPosition.z != parallax.z) { transform.localPosition = parallax; scene.Transforms().Set(chunk.entity, transform); }
        }
        // Replace only the legacy map cache. An ordinary mesh is an independent authored component.
        if (const auto* mesh = query.MeshRenderers().TryGet(owner);
            instance.dirty.empty() && mesh && mesh->meshAssetId == source) scene.Components().MeshRenderers().Remove(owner);
    }
}

void Map2DRuntime::OnDestroy(kb::scene::SceneSystemContext& context) {
    auto& scene = context.GetScene();
    for (auto& [owner, instance] : state_->instances) {
        State::Release(scene, instance);
        if (scene.Entities().IsAlive(owner) && instance.originalRenderer && !scene.Components().MeshRenderers().Has(owner))
            scene.Components().MeshRenderers().Set(owner, *instance.originalRenderer);
    }
    state_->instances.clear();
    if (scene.Runtime().Tilemaps() == this) scene.Runtime().SetTilemapRuntime(nullptr);
}

} // namespace kb::world2d
