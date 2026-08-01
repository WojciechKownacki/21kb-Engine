#include "scene/EditorTerrainService.hpp"

#include "TerrainHeightmapImporter.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string value = path.extension().string();
    std::ranges::transform(value, value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

[[nodiscard]] const kb::assets::AssetMetadata* TerrainMetadata(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity) noexcept {
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr || renderer->meshAssetId == 0U) return nullptr;
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ renderer->meshAssetId });
    return metadata != nullptr && LowerExtension(metadata->physicalPath) == kb::assets::kTerrainAssetExtension ? metadata : nullptr;
}

[[nodiscard]] std::filesystem::path UniqueTerrainPath(const std::filesystem::path& assetsRoot) {
    const std::filesystem::path directory = assetsRoot / "Terrains";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    for (std::uint32_t suffix = 0U; suffix < 100'000U; ++suffix) {
        const std::string name = suffix == 0U ? "Terrain" : "Terrain " + std::to_string(suffix + 1U);
        const std::filesystem::path candidate = directory / (name + std::string{ kb::assets::kTerrainAssetExtension });
        if (!std::filesystem::exists(candidate, error)) return candidate;
        error.clear();
    }
    return {};
}

[[nodiscard]] bool SaveAndRefresh(
    kb::scene::Scene& scene,
    const kb::assets::AssetMetadata& sourceMetadata,
    const kb::assets::TerrainAsset& terrain,
    std::string* error) {
    if (!kb::assets::TerrainAssetIO::Save(sourceMetadata.physicalPath, terrain, error)) return false;
    kb::assets::AssetMetadata updated = sourceMetadata;
    ++updated.contentHash;
    if (updated.contentHash == 0U) updated.contentHash = 1U;
    static_cast<void>(scene.Assets().Manager().RegisterAsset(std::move(updated)));
    return true;
}

} // namespace

EditorTerrainToolState& EditorTerrainService::ToolState() noexcept {
    static EditorTerrainToolState state{};
    return state;
}

bool EditorTerrainService::IsTerrainEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    return TerrainMetadata(scene, entity) != nullptr;
}

std::optional<kb::assets::TerrainAsset> EditorTerrainService::Load(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = TerrainMetadata(scene, entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "Entity does not reference a terrain asset";
        return std::nullopt;
    }
    return kb::assets::TerrainAssetIO::Load(metadata->physicalPath, error);
}

bool EditorTerrainService::Create(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const std::filesystem::path& assetsRoot,
    std::string* error) {
    if (!scene.Entities().IsAlive(entity)) {
        if (error != nullptr) *error = "Terrain owner entity is not alive";
        return false;
    }
    if (scene.Components().MeshRenderers().Has(entity)) {
        if (error != nullptr) *error = IsTerrainEntity(scene, entity)
            ? "Entity already has a Terrain Editor component"
            : "Terrain Editor needs the entity's Mesh Renderer slot; use a separate entity";
        return false;
    }
    const std::filesystem::path path = UniqueTerrainPath(assetsRoot);
    if (path.empty()) {
        if (error != nullptr) *error = "A unique terrain asset path could not be allocated";
        return false;
    }
    const kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset();
    if (!kb::assets::TerrainAssetIO::Save(path, terrain, error)) return false;
    static_cast<void>(scene.Assets().Discover());
    const auto metadata = std::ranges::find_if(scene.Assets().Manager().Registry().All(), [&path](const kb::assets::AssetMetadata& candidate) {
        std::error_code leftError;
        std::error_code rightError;
        return std::filesystem::weakly_canonical(candidate.physicalPath, leftError) == std::filesystem::weakly_canonical(path, rightError) &&
            !leftError && !rightError;
    });
    if (metadata == scene.Assets().Manager().Registry().All().end()) {
        if (error != nullptr) *error = "Terrain asset was saved but asset discovery did not register it";
        return false;
    }
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = metadata->id.value,
        .castsShadow = true,
        .receivesShadow = true,
    });
    return true;
}

bool EditorTerrainService::Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    if (!IsTerrainEntity(scene, entity)) return false;
    scene.Components().MeshRenderers().Remove(entity);
    return true;
}

bool EditorTerrainService::ApplyBrush(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const kb::terrain_editor::TerrainBrushSettings& settings,
    const kb::terrain_editor::TerrainBrushStamp& stamp,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = TerrainMetadata(scene, entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "Terrain asset is not available";
        return false;
    }
    const kb::assets::AssetMetadata metadataCopy = *metadata;
    std::optional<kb::assets::TerrainAsset> terrain = kb::assets::TerrainAssetIO::Load(metadataCopy.physicalPath, error);
    if (!terrain.has_value()) return false;
    if (!kb::terrain_editor::ApplyTerrainBrush(*terrain, settings, stamp).Changed()) return true;
    return SaveAndRefresh(scene, metadataCopy, *terrain, error);
}

bool EditorTerrainService::ImportHeightmap(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    const std::filesystem::path& heightmapPath,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = TerrainMetadata(scene, entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "Terrain asset is not available";
        return false;
    }
    const kb::assets::AssetMetadata metadataCopy = *metadata;
    std::optional<kb::assets::TerrainAsset> imported = kb::terrain_editor::TerrainHeightmapImporter::Import(heightmapPath, {}, error);
    if (!imported.has_value()) return false;
    if (std::optional<kb::assets::TerrainAsset> current = kb::assets::TerrainAssetIO::Load(metadataCopy.physicalPath)) {
        imported->worldSizeX = current->worldSizeX;
        imported->worldSizeZ = current->worldSizeZ;
        imported->chunkQuads = current->chunkQuads;
        imported->lodCount = current->lodCount;
    }
    return SaveAndRefresh(scene, metadataCopy, *imported, error);
}

} // namespace kb::editor
