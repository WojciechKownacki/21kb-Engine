#include "scene/EditorMap2DService.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace kb::editor {
namespace {
using namespace kb::assets; using namespace kb::scene;
struct PreviewChunk {
    SceneEntity entity{};
    AssetId mesh{}, shadowMesh{};
    std::map<std::uint64_t, Map2DTile> tiles;
    std::string settings;
    std::uint64_t frame = 0;
};
struct PreviewLayer { AssetId source{}; std::map<std::uint64_t, PreviewChunk> chunks; };
std::map<Scene*, std::map<SceneEntity, PreviewLayer>>& Previews() {
    static std::map<Scene*, std::map<SceneEntity, PreviewLayer>> previews;
    return previews;
}
void Release(Scene& scene, PreviewChunk& chunk) {
    if (scene.Entities().IsAlive(chunk.entity)) scene.Entities().Destroy(chunk.entity);
    auto& assets = scene.Assets().Manager();
    if (chunk.mesh.IsValid()) static_cast<void>(assets.ReleaseGeneratedAsset(chunk.mesh));
    if (chunk.shadowMesh.IsValid()) static_cast<void>(assets.ReleaseGeneratedAsset(chunk.shadowMesh));
    chunk.entity = {}; chunk.mesh = {}; chunk.shadowMesh = {};
}
std::uint64_t Frame(const Map2DAsset& map, double seconds) {
    std::uint64_t hash = 0;
    const auto add = [&](Map2DSprite sprite) {
        const auto* definition = Map2DDefinition(map, sprite);
        if (definition && definition->frames.size() > 1U) hash = hash * 1099511628211ULL + Map2DAnimatedSprite(map, sprite, seconds).cell + 1U;
    };
    for (const auto& [key, tile] : map.tiles) { static_cast<void>(key); add(tile.ground); for (auto sprite : tile.borders) add(sprite); for (auto sprite : tile.items) add(sprite); }
    return hash;
}
}

void EditorMap2DService::ClearPreviews(kb::scene::Scene& scene) {
    const auto found = Previews().find(&scene); if (found == Previews().end()) return;
    for (auto& [owner, layer] : found->second) {
        for (auto& [key, chunk] : layer.chunks) { static_cast<void>(key); Release(scene, chunk); }
        if (const auto* renderer = std::as_const(scene).Components().MeshRenderers().TryGet(owner);
            renderer && renderer->previewHidden && IsMapMeshRenderer(scene, owner)) {
            auto visible = *renderer; visible.previewHidden = false; scene.Components().MeshRenderers().Set(owner, visible);
        }
    }
    Previews().erase(found);
}

bool EditorMap2DService::RefreshPreview(kb::scene::Scene& scene, kb::assets::AssetId assetId,
    const kb::assets::Map2DAsset& map, double seconds, std::string* error) {
    if (!IsMap2DAssetValid(map, error)) return false;
    auto& cache = Previews()[&scene]; auto& manager = scene.Assets().Manager();
    for (auto it = cache.begin(); it != cache.end();) {
        const auto* tilemap = std::as_const(scene).Components().Tilemaps().TryGet(it->first);
        if (tilemap && tilemap->mapAssetId == it->second.source.value) { ++it; continue; }
        for (auto& [key, chunk] : it->second.chunks) { static_cast<void>(key); Release(scene, chunk); }
        it = cache.erase(it);
    }
    std::vector<SceneEntity> owners;
    struct Query { std::uint64_t id; std::vector<SceneEntity>* owners; } query{assetId.value, &owners};
    scene.Runtime().EcsWorld().CreateQuery<TilemapComponent>().ForEach([](SceneEntity entity, const TilemapComponent& value, void* context) {
        auto& state = *static_cast<Query*>(context); if (value.mapAssetId == state.id) state.owners->push_back(entity);
    }, &query);
    std::map<std::uint64_t, std::map<std::uint64_t, Map2DTile>> cells;
    for (const auto& [key, tile] : map.tiles) cells[Map2DTileKey(Map2DTileKeyX(key) / 16U, Map2DTileKeyY(key) / 16U, Map2DTileKeyZ(key))].emplace(key, tile);
    if (map.background) {
        cells[UINT64_MAX];
        if (!map.tiles.empty()) cells[UINT64_MAX][map.tiles.begin()->first].solid = true;
    }
    auto fragment = map; fragment.tiles.clear(); fragment.background = false;
    const auto settings = Map2DAssetIO::Write(fragment);
    for (const auto owner : owners) {
        auto& layer = cache[owner]; layer.source = assetId;
        for (auto it = layer.chunks.begin(); it != layer.chunks.end();) {
            if (cells.contains(it->first)) { ++it; continue; }
            Release(scene, it->second); it = layer.chunks.erase(it);
        }
        const auto* renderer = std::as_const(scene).Components().TilemapRenderers().TryGet(owner);
        const float lift = 0;
        for (const auto& [key, tiles] : cells) {
            fragment.tiles = tiles; fragment.background = key == UINT64_MAX;
            const auto frame = Frame(fragment, seconds);
            auto& chunk = layer.chunks[key];
            const auto identity = settings + ":" + std::to_string(lift) + ":" + (renderer && !renderer->enabled ? "hidden" : "visible");
            if (chunk.settings == identity && chunk.tiles == tiles && chunk.frame == frame && scene.Entities().IsAlive(chunk.entity) && manager.AcquireLoaded<kb::render::RenderMeshAssetData>(chunk.mesh).IsLoaded()) continue;
            auto mesh = kb::render::RenderMap2DMeshBuilder::Build(fragment, lift, seconds);
            if (!mesh) { if (error) *error = "Could not build tilemap preview chunk"; return false; }
            Release(scene, chunk);
            chunk.entity = scene.Entities().CreateEntity({.name = "Tilemap Preview", .parent = scene.Entities().Object(owner)});
            const auto path = "/Runtime/TilemapPreview/" + std::to_string(owner.Id()) + "/" + std::to_string(key);
            chunk.mesh = MakeAssetId(path);
            if (!manager.RegisterGeneratedAsset({.id = chunk.mesh, .type = "RenderMesh", .virtualPath = path, .contentHash = manager.Registry().Generation()}) ||
                !manager.PublishRuntimeAsset(chunk.mesh, std::make_shared<kb::render::RenderMeshAssetData>(std::move(*mesh)))) { if (error) *error = manager.LastError(); return false; }
            scene.Components().MeshRenderers().Set(chunk.entity, {.meshAssetId = chunk.mesh.value, .castsShadow = false, .transient = true});
            if (auto shadow = kb::render::RenderMap2DMeshBuilder::BuildOccluders(fragment, lift, seconds); shadow && (!renderer || renderer->enabled)) {
                const auto child = scene.Entities().CreateEntity({.name = "Tilemap Occlusion", .parent = scene.Entities().Object(chunk.entity)});
                chunk.shadowMesh = MakeAssetId(path + ":occlusion");
                if (!manager.RegisterGeneratedAsset({.id = chunk.shadowMesh, .type = "RenderMesh", .virtualPath = path + "/Occlusion", .contentHash = manager.Registry().Generation()}) ||
                    !manager.PublishRuntimeAsset(chunk.shadowMesh, std::make_shared<kb::render::RenderMeshAssetData>(std::move(*shadow)))) { if (error) *error = manager.LastError(); return false; }
                scene.Components().MeshRenderers().Set(child, {.meshAssetId = chunk.shadowMesh.value, .shadowOnly = true, .transient = true});
            }
            for (const auto& [cell, tile] : tiles) {
                if (tile.prefab.empty()) continue;
                const auto prefab = scene.Assets().LoadPrefab(Utf8Path(tile.prefab));
                if (!prefab.IsLoaded()) { if (error) *error = "Could not load prefab " + tile.prefab; return false; }
                const auto instance = scene.Prefabs().Instantiate(*prefab.Get(), {.parent = scene.Entities().Object(chunk.entity)});
                if (instance.Empty()) continue;
                const auto entity = instance.RootObject().Entity(); auto transform = scene.Transforms().Get(entity);
                const float x = (static_cast<float>(Map2DTileKeyX(cell)) + 0.5F - static_cast<float>(map.width) * 0.5F) * map.tileSize;
                const float y = static_cast<float>(Map2DTileKeyY(cell)) + 1.0F, elevation = static_cast<float>(Map2DTileKeyZ(cell)) * map.floorHeight;
                transform.localPosition = transform.localPosition + (map.orientation == Map2DOrientation::Wall
                    ? Vec3{x,(static_cast<float>(map.height)-y)*map.tileSize,-elevation}
                    : Vec3{x,elevation,(static_cast<float>(map.height)*0.5F-y)*map.tileSize});
                scene.Transforms().Set(entity, transform);
            }
            chunk.tiles = tiles; chunk.settings = identity; chunk.frame = frame;
        }
        if (const auto* original = std::as_const(scene).Components().MeshRenderers().TryGet(owner);
            original && !original->previewHidden && IsMapMeshRenderer(scene, owner)) {
            auto hidden = *original; hidden.previewHidden = true; scene.Components().MeshRenderers().Set(owner, hidden);
        }
    }
    return true;
}
} // namespace kb::editor
