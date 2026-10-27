#include "scene/EditorMap2DService.hpp"

#include "Map2DEditing.hpp"
#include "Map2DView.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsMapFile(const std::filesystem::path& path) {
    std::string value = path.extension().string();
    std::ranges::transform(value, value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value == kb::assets::kMap2DAssetExtension;
}

[[nodiscard]] std::filesystem::path UniqueMapPath(const std::filesystem::path& assetsRoot) {
    const std::filesystem::path directory = assetsRoot / "Maps";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    for (std::uint32_t suffix = 0U; suffix < 100'000U; ++suffix) {
        const std::string name = suffix == 0U ? "World" : "World " + std::to_string(suffix + 1U);
        const std::filesystem::path candidate = directory / (name + std::string{ kb::assets::kMap2DAssetExtension });
        if (!std::filesystem::exists(candidate, error)) return candidate;
        error.clear();
    }
    return {};
}

} // namespace

const kb::assets::AssetMetadata* EditorMap2DService::MapMetadata(const kb::scene::Scene& scene, kb::assets::AssetId assetId) noexcept {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && IsMapFile(metadata->physicalPath) ? metadata : nullptr;
}

const kb::assets::AssetMetadata* EditorMap2DService::MapMetadata(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    if (const auto* tilemap = scene.Components().Tilemaps().TryGet(entity)) {
        return MapMetadata(scene, kb::assets::AssetId{ tilemap->mapAssetId });
    }
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr || renderer->meshAssetId == 0U) return nullptr;
    return MapMetadata(scene, kb::assets::AssetId{ renderer->meshAssetId });
}

bool EditorMap2DService::IsMapEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    return MapMetadata(scene, entity) != nullptr;
}

bool EditorMap2DService::IsMapMeshRenderer(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    const auto* renderer = scene.Components().MeshRenderers().TryGet(entity);
    return renderer && MapMetadata(scene, kb::assets::AssetId{renderer->meshAssetId}) != nullptr;
}

bool EditorMap2DService::Create(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const std::filesystem::path& assetsRoot,
    std::string* error) {
    if (!scene.Entities().IsAlive(entity)) {
        if (error != nullptr) *error = "World 2D Map owner entity is not alive";
        return false;
    }
    if (scene.Components().Tilemaps().Has(entity) || IsMapEntity(scene, entity)) {
        if (error != nullptr) *error = "Entity already has a World 2D Map component";
        return false;
    }
    const std::filesystem::path path = UniqueMapPath(assetsRoot);
    if (path.empty()) {
        if (error != nullptr) *error = "A unique map asset path could not be allocated";
        return false;
    }
    if (!kb::assets::Map2DAssetIO::Save(path, kb::assets::MakeEmptyMap2DAsset(), error)) return false;
    // Where the Map Editor's palette looks first for sprite sheets.
    std::error_code folderError;
    std::filesystem::create_directories(assetsRoot / kb::assets::Utf8Path(kb::world2d::kMap2DSpritesFolder), folderError);
    static_cast<void>(scene.Assets().Discover());
    const auto metadata = std::ranges::find_if(scene.Assets().Manager().Registry().All(), [&path](const kb::assets::AssetMetadata& candidate) {
        std::error_code leftError;
        std::error_code rightError;
        return std::filesystem::weakly_canonical(candidate.physicalPath, leftError) == std::filesystem::weakly_canonical(path, rightError) &&
            !leftError && !rightError;
    });
    if (metadata == scene.Assets().Manager().Registry().All().end()) {
        if (error != nullptr) *error = "Map asset was saved but asset discovery did not register it";
        return false;
    }
    const kb::assets::AssetId mapId = metadata->id;
    kb::world2d::SetUpMap2DComponents(scene, entity, mapId.value, kb::assets::MakeEmptyMap2DAsset());
    // The mesh is there at once, so the viewport frames the whole map.
    static_cast<void>(Publish(scene, mapId, kb::assets::MakeEmptyMap2DAsset(), false, nullptr));
    return true;
}

std::optional<kb::assets::Map2DAsset> EditorMap2DService::Load(
    const kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = MapMetadata(scene, assetId);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "The map asset is not registered";
        return std::nullopt;
    }
    return kb::assets::Map2DAssetIO::Load(metadata->physicalPath, error);
}

bool EditorMap2DService::SyncCollision(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::Map2DAsset& map,
    bool* changed,
    std::string* error) {
    if (changed != nullptr) *changed = false;
    // Collect owners before rebuilding children: hierarchy mutations invalidate traversal.
    std::vector<kb::scene::SceneEntity> pending = scene.Hierarchy().RootEntities();
    std::vector<kb::scene::SceneEntity> owners;
    while (!pending.empty()) {
        const auto entity = pending.back();
        pending.pop_back();
        const auto* metadata = MapMetadata(scene, entity);
        if (metadata != nullptr && metadata->id == assetId) owners.push_back(entity);
        const auto children = scene.Hierarchy().ChildEntities(entity);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    for (const auto entity : owners) {
        kb::world2d::SetUpMap2DComponents(scene, entity, assetId.value, map);
        bool rebuilt = false;
        const auto* collider = std::as_const(scene).Components().TilemapColliders().TryGet(entity);
        if (collider && !collider->enabled) {
            auto empty = map;
            empty.tiles.clear();
            if (!kb::world2d::SetUpMap2DCollision(scene, entity, empty, &rebuilt, error)) return false;
        } else if (!kb::world2d::SetUpMap2DCollision(scene, entity, map, &rebuilt, error)) return false;
        if (changed != nullptr && rebuilt) *changed = true;
    }
    return true;
}

bool EditorMap2DService::Publish(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::Map2DAsset& map,
    bool persist,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = MapMetadata(scene, assetId);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "The map asset is not registered";
        return false;
    }
    if (!kb::assets::IsMap2DAssetValid(map, error)) return false;
    const kb::assets::AssetMetadata copy = *metadata;
    if (persist && !kb::assets::Map2DAssetIO::Save(copy.physicalPath, map, error)) return false;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    // A sheet painted for the first time brings a new sprite material: the scene has to know it
    // before the mesh asks for it.
    for (const kb::assets::Map2DSheet& sheet : map.sheets) {
        const kb::assets::AssetId material{ kb::render::RenderMap2DMeshBuilder::MaterialAssetId(sheet.material) };
        if (material.IsValid() && manager.Registry().Find(material) == nullptr) {
            static_cast<void>(scene.Assets().Discover());
            break;
        }
    }
    kb::assets::AssetMetadata updated = copy;
    ++updated.contentHash;
    if (updated.contentHash == 0U) updated.contentHash = 1U;
    if (!manager.RegisterAsset(std::move(updated))) {
        if (error != nullptr) *error = "The map asset metadata could not be refreshed";
        return false;
    }
    if (persist) {
        auto built = kb::render::RenderMap2DMeshBuilder::Build(map);
        if (!built || !manager.PublishRuntimeAsset<kb::render::RenderMeshAssetData>(assetId,
            std::make_shared<kb::render::RenderMeshAssetData>(std::move(*built)))) {
            if (error != nullptr) *error = manager.LastError();
            return false;
        }
    }
    if (!RefreshPreview(scene, assetId, map, 0, error)) return false;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace kb::editor
