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
#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
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

[[nodiscard]] const kb::assets::AssetMetadata* TerrainMetadata(
    const kb::scene::Scene& scene,
    kb::assets::AssetId assetId) noexcept {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && LowerExtension(metadata->physicalPath) == kb::assets::kTerrainAssetExtension
        ? metadata
        : nullptr;
}

[[nodiscard]] bool Refresh(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::shared_ptr<kb::render::RenderMeshAssetData> mesh,
    bool persist,
    bool refreshMetadata,
    std::string* error) {
    const kb::assets::AssetMetadata* sourceMetadata = TerrainMetadata(scene, assetId);
    if (sourceMetadata == nullptr) {
        if (error != nullptr) *error = "Terrain asset is not registered";
        return false;
    }
    if (mesh == nullptr) {
        std::optional<kb::render::RenderMeshAssetData> built =
            kb::render::RenderTerrainMeshBuilder::Build(terrain);
        if (built.has_value()) {
            mesh = std::make_shared<kb::render::RenderMeshAssetData>(std::move(*built));
        }
    }
    if (mesh == nullptr) {
        if (error != nullptr) *error = "Terrain preview mesh could not be built";
        return false;
    }
    const kb::assets::AssetMetadata sourceCopy = *sourceMetadata;
    if (persist && !kb::assets::TerrainAssetIO::Save(sourceCopy.physicalPath, terrain, error)) {
        return false;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (refreshMetadata) {
        kb::assets::AssetMetadata updated = sourceCopy;
        ++updated.contentHash;
        if (updated.contentHash == 0U) updated.contentHash = 1U;
        if (!manager.RegisterAsset(std::move(updated))) {
            if (error != nullptr) *error = "Terrain asset metadata could not be refreshed";
            return false;
        }
    }
    if (!manager.PublishRuntimeAsset<kb::render::RenderMeshAssetData>(
            assetId,
            std::move(mesh))) {
        if (error != nullptr) *error = manager.LastError();
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

[[nodiscard]] bool Refresh(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    bool persist,
    std::string* error) {
    return Refresh(scene, assetId, terrain, nullptr, persist, true, error);
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

std::optional<EditorTerrainAssetState> EditorTerrainService::Capture(
    const kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::string* error) {
    const kb::assets::AssetMetadata* metadata = TerrainMetadata(scene, entity);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "Entity does not reference a terrain asset";
        return std::nullopt;
    }
    std::optional<kb::assets::TerrainAsset> terrain =
        kb::assets::TerrainAssetIO::Load(metadata->physicalPath, error);
    if (!terrain.has_value()) return std::nullopt;
    return EditorTerrainAssetState{
        .assetId = metadata->id,
        .terrain = std::move(*terrain),
    };
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

bool EditorTerrainService::PublishPreview(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::string* error) {
    return Refresh(scene, assetId, terrain, false, error);
}

std::shared_ptr<kb::render::RenderMeshAssetData> EditorTerrainService::CreatePreviewMesh(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::string* error) {
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> current =
        scene.Assets().Manager().Load<kb::render::RenderMeshAssetData>(assetId);
    std::shared_ptr<kb::render::RenderMeshAssetData> preview;
    if (current.IsLoaded()) {
        preview = std::make_shared<kb::render::RenderMeshAssetData>(*current);
        preview->RefreshDesc();
    }
    if (preview == nullptr ||
        !kb::render::RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *preview)) {
        std::optional<kb::render::RenderMeshAssetData> built =
            kb::render::RenderTerrainMeshBuilder::Build(terrain);
        if (!built.has_value()) {
            if (error != nullptr) *error = "Terrain preview mesh could not be built";
            return {};
        }
        preview = std::make_shared<kb::render::RenderMeshAssetData>(std::move(*built));
        if (!kb::render::RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *preview)) {
            if (error != nullptr) *error = "Terrain preview mesh is not dynamically updateable";
            return {};
        }
    }
    if (error != nullptr) error->clear();
    return preview;
}

std::shared_ptr<kb::render::RenderMeshAssetData> EditorTerrainService::CreateLayerPreviewMesh(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::string* error) {
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> current =
        scene.Assets().Manager().Load<kb::render::RenderMeshAssetData>(assetId);
    if (current.IsLoaded() && current->terrainLayerCount == terrain.materialLayers.size() &&
        current->terrainLayerWeightWidth == terrain.layerWeightWidth &&
        current->terrainLayerWeightHeight == terrain.layerWeightHeight &&
        current->terrainLayerWeights.size() == terrain.layerWeights.size()) {
        // Runtime terrain assets are immutable to ordinary consumers. The editor publishes this same
        // payload as its explicitly-owned working copy, so weight edits can stay subresource-only instead
        // of cloning every vertex/index/LOD buffer on every mouse press.
        std::shared_ptr<kb::render::RenderMeshAssetData> preview =
            std::const_pointer_cast<kb::render::RenderMeshAssetData>(current.Shared());
        if (error != nullptr) error->clear();
        return preview;
    }
    return CreatePreviewMesh(scene, assetId, terrain, error);
}

bool EditorTerrainService::UpdatePreviewMesh(
    const kb::assets::TerrainAsset& terrain,
    const kb::terrain_editor::TerrainBrushResult& changedRegion,
    bool topologyChanged,
    std::shared_ptr<kb::render::RenderMeshAssetData>& mesh,
    std::string* error) {
    const bool updated = !topologyChanged && mesh != nullptr &&
        kb::render::RenderTerrainMeshBuilder::UpdateDynamicPreview(
            terrain,
            kb::render::RenderTerrainMeshUpdateRegion{
                .minX = changedRegion.minX,
                .minZ = changedRegion.minZ,
                .maxX = changedRegion.maxX,
                .maxZ = changedRegion.maxZ,
            },
            *mesh);
    if (updated) {
        if (error != nullptr) error->clear();
        return true;
    }
    std::optional<kb::render::RenderMeshAssetData> rebuilt =
        kb::render::RenderTerrainMeshBuilder::Build(terrain);
    if (!rebuilt.has_value()) {
        if (error != nullptr) *error = "Terrain preview mesh could not be rebuilt";
        return false;
    }
    mesh = std::make_shared<kb::render::RenderMeshAssetData>(std::move(*rebuilt));
    if (!kb::render::RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *mesh)) {
        if (error != nullptr) *error = "Terrain preview mesh is not dynamically updateable";
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

bool EditorTerrainService::UpdateLayerPreviewMesh(
    const kb::assets::TerrainAsset& terrain,
    const kb::terrain_editor::TerrainLayerPaintResult& changedRegion,
    std::shared_ptr<kb::render::RenderMeshAssetData>& mesh,
    std::string* error) {
    const bool updated = mesh != nullptr &&
        kb::render::RenderTerrainMeshBuilder::UpdateDynamicLayerPreview(
            terrain,
            kb::render::RenderTerrainLayerWeightUpdateRegion{
                .x = static_cast<std::uint16_t>(changedRegion.minX),
                .y = static_cast<std::uint16_t>(changedRegion.minY),
                .width = static_cast<std::uint16_t>(changedRegion.maxX - changedRegion.minX + 1U),
                .height = static_cast<std::uint16_t>(changedRegion.maxY - changedRegion.minY + 1U),
            },
            *mesh);
    if (updated) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (error != nullptr) *error = "Terrain layer preview could not be updated";
    return false;
}

bool EditorTerrainService::PublishPreview(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::shared_ptr<kb::render::RenderMeshAssetData> mesh,
    bool initializeDynamicResource,
    std::string* error) {
    return Refresh(
        scene, assetId, terrain, std::move(mesh), false,
        initializeDynamicResource, error);
}

bool EditorTerrainService::Persist(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::string* error) {
    return Refresh(scene, assetId, terrain, true, error);
}

bool EditorTerrainService::Persist(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    std::shared_ptr<kb::render::RenderMeshAssetData> mesh,
    std::string* error) {
    return Refresh(scene, assetId, terrain, std::move(mesh), true, true, error);
}

std::optional<kb::assets::TerrainAsset> EditorTerrainService::BuildHeightmapImport(
    const kb::assets::TerrainAsset& current,
    const std::filesystem::path& heightmapPath,
    const kb::terrain_editor::TerrainHeightmapImportSettings& settings,
    std::string* error) {
    std::optional<kb::assets::TerrainAsset> imported =
        kb::terrain_editor::TerrainHeightmapImporter::Import(heightmapPath, settings, error);
    if (!imported.has_value()) return std::nullopt;
    imported->worldSizeX = current.worldSizeX;
    imported->worldSizeZ = current.worldSizeZ;
    imported->chunkQuads = current.chunkQuads;
    imported->lodCount = current.lodCount;
    imported->materialLayers = current.materialLayers;
    imported->layerWeightWidth = current.layerWeightWidth;
    imported->layerWeightHeight = current.layerWeightHeight;
    imported->layerWeights = current.layerWeights;
    return imported;
}

std::optional<kb::assets::TerrainAsset> EditorTerrainService::BuildReconfigured(
    const kb::assets::TerrainAsset& current,
    const EditorTerrainConfiguration& configuration,
    std::string* error) {
    if (!kb::assets::IsTerrainAssetValid(current, error)) return std::nullopt;
    if (!kb::assets::IsTerrainResolutionValid(configuration.width) ||
        !kb::assets::IsTerrainResolutionValid(configuration.height)) {
        if (error != nullptr) {
            *error = "Terrain resolution must be 2^n + 1 from 17 to 2049";
        }
        return std::nullopt;
    }
    const bool chunkQuadsValid =
        configuration.chunkQuads >= 8U && configuration.chunkQuads <= 128U &&
        (configuration.chunkQuads & (configuration.chunkQuads - 1U)) == 0U;
    if (!chunkQuadsValid || configuration.lodCount == 0U ||
        configuration.lodCount > 8U ||
        !std::isfinite(configuration.worldSizeX) ||
        !std::isfinite(configuration.worldSizeZ) ||
        configuration.worldSizeX <= 0.0F || configuration.worldSizeZ <= 0.0F) {
        if (error != nullptr) *error = "Terrain size, chunk or LOD configuration is invalid";
        return std::nullopt;
    }

    kb::assets::TerrainAsset result{};
    result.width = configuration.width;
    result.height = configuration.height;
    result.chunkQuads = configuration.chunkQuads;
    result.lodCount = configuration.lodCount;
    result.worldSizeX = configuration.worldSizeX;
    result.worldSizeZ = configuration.worldSizeZ;
    result.materialLayers = current.materialLayers;
    result.layerWeightWidth = current.layerWeightWidth;
    result.layerWeightHeight = current.layerWeightHeight;
    result.layerWeights = current.layerWeights;
    result.heights.resize(
        static_cast<std::size_t>(result.width) * result.height);
    result.holes.resize(
        static_cast<std::size_t>(result.width - 1U) * (result.height - 1U));

    const auto sourceHeight = [&current](std::uint32_t x, std::uint32_t z) {
        return current.heights[static_cast<std::size_t>(z) * current.width + x];
    };
    for (std::uint32_t z = 0U; z < result.height; ++z) {
        const float sourceZ =
            static_cast<float>(z) * static_cast<float>(current.height - 1U) /
            static_cast<float>(result.height - 1U);
        const std::uint32_t z0 = static_cast<std::uint32_t>(sourceZ);
        const std::uint32_t z1 = std::min(z0 + 1U, current.height - 1U);
        const float tz = sourceZ - static_cast<float>(z0);
        for (std::uint32_t x = 0U; x < result.width; ++x) {
            const float sourceX =
                static_cast<float>(x) * static_cast<float>(current.width - 1U) /
                static_cast<float>(result.width - 1U);
            const std::uint32_t x0 = static_cast<std::uint32_t>(sourceX);
            const std::uint32_t x1 = std::min(x0 + 1U, current.width - 1U);
            const float tx = sourceX - static_cast<float>(x0);
            result.heights[static_cast<std::size_t>(z) * result.width + x] =
                std::lerp(
                    std::lerp(sourceHeight(x0, z0), sourceHeight(x1, z0), tx),
                    std::lerp(sourceHeight(x0, z1), sourceHeight(x1, z1), tx),
                    tz);
        }
    }

    for (std::uint32_t z = 0U; z + 1U < result.height; ++z) {
        const std::uint32_t sourceZ = std::min(
            static_cast<std::uint32_t>(
                ((static_cast<std::uint64_t>(z) * 2U + 1U) *
                 (current.height - 1U)) /
                (static_cast<std::uint64_t>(result.height - 1U) * 2U)),
            current.height - 2U);
        for (std::uint32_t x = 0U; x + 1U < result.width; ++x) {
            const std::uint32_t sourceX = std::min(
                static_cast<std::uint32_t>(
                    ((static_cast<std::uint64_t>(x) * 2U + 1U) *
                     (current.width - 1U)) /
                    (static_cast<std::uint64_t>(result.width - 1U) * 2U)),
                current.width - 2U);
            result.holes[static_cast<std::size_t>(z) * (result.width - 1U) + x] =
                current.holes[
                    static_cast<std::size_t>(sourceZ) * (current.width - 1U) +
                    sourceX];
        }
    }

    if (!kb::assets::IsTerrainAssetValid(result, error)) return std::nullopt;
    if (error != nullptr) error->clear();
    return result;
}

} // namespace kb::editor
