#include "scene/EditorTerrainService.hpp"

#include "TerrainHeightmapImporter.hpp"
#include "TerrainLayerPainter.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include <algorithm>
#include <array>
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

// The ground a new terrain wears: grass on all of it, and rock to paint on cliffs (Auto painting gives steep
// ground the last layer). The materials are shared by every new terrain: one that exists is used as it is.
struct DefaultTerrainLayer {
    std::string_view name;
    std::array<float, 3U> color;
    float roughness;
};
constexpr std::array<DefaultTerrainLayer, 2U> kDefaultTerrainLayers{ {
    { "Terrain Grass", { 0.22F, 0.38F, 0.10F }, 0.95F },
    { "Terrain Rock", { 0.38F, 0.36F, 0.33F }, 0.90F },
} };

[[nodiscard]] const kb::assets::AssetMetadata* FindAssetByPath(const kb::scene::Scene& scene, const std::filesystem::path& path) {
    const auto found = std::ranges::find_if(scene.Assets().Manager().Registry().All(), [&path](const kb::assets::AssetMetadata& candidate) {
        std::error_code leftError;
        std::error_code rightError;
        return std::filesystem::weakly_canonical(candidate.physicalPath, leftError) == std::filesystem::weakly_canonical(path, rightError) &&
            !leftError && !rightError;
    });
    return found == scene.Assets().Manager().Registry().All().end() ? nullptr : &*found;
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
    // A preview mesh that comes with the terrain was kept up to date by the brush, which told the physics world
    // as it went; a terrain without one changed as a whole.
    const bool wholeTerrainChanged = mesh == nullptr;
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
    if (wholeTerrainChanged) {
        static_cast<void>(kb::scene::PhysicsBackend::UpdateTerrain(
            scene, assetId.value, terrain, kb::assets::TerrainSampleRegion{ 0U, 0U, terrain.width - 1U, terrain.height - 1U }));
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

EditorTerrainTool EditorTerrainService::ActiveTool(const EditorTerrainToolState& tool) noexcept {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    if (!tool.editingEnabled || tool.mode == EditorTerrainToolMode::Select) return EditorTerrainTool::None;
    if (tool.mode == EditorTerrainToolMode::Paint) return EditorTerrainTool::Paint;
    if (tool.mode == EditorTerrainToolMode::Holes) return EditorTerrainTool::Holes;
    if (tool.mode == EditorTerrainToolMode::River) return EditorTerrainTool::River;
    switch (tool.brush.mode) {
    case Mode::Smooth:
    case Mode::Flatten: return EditorTerrainTool::Smooth;
    case Mode::Ramp: return EditorTerrainTool::Ramp;
    case Mode::Stamp: return EditorTerrainTool::Stamp;
    default: return EditorTerrainTool::Sculpt;
    }
}

bool EditorTerrainService::SelectTool(
    EditorTerrainToolState& tool, EditorTerrainTool selected, const kb::assets::TerrainAsset* terrain, std::string* error) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    if (error != nullptr) error->clear();
    tool.brushMenuOpen = false;
    tool.brushShapeMenuOpen = false;
    if (selected == EditorTerrainTool::None || selected == ActiveTool(tool)) {
        tool.mode = EditorTerrainToolMode::Select;
        tool.editingEnabled = false;
        tool.strokeActive = false;
        tool.rampDragging = false;
        tool.riverDragPoint = -1;
        tool.riverPendingVisible = false;
        tool.hoverVisible = false;
        return true;
    }
    if (selected == EditorTerrainTool::Paint) {
        if (terrain == nullptr || terrain->materialLayers.empty()) {
            if (error != nullptr) *error = "Add a material layer in the Inspector before painting.";
            return false;
        }
        tool.selectedMaterialLayer = std::min<std::uint8_t>(tool.selectedMaterialLayer, static_cast<std::uint8_t>(terrain->materialLayers.size() - 1U));
        tool.brush.strength = std::min(tool.brush.strength, 1.0F);
    } else if (selected == EditorTerrainTool::Stamp && tool.brush.stampImage == nullptr) {
        if (error != nullptr) *error = "Pick a height image for the stamp first.";
        return false;
    }
    tool.drawer = tool.drawer == EditorTerrainDrawer::NewWorld ? EditorTerrainDrawer::None : tool.drawer;
    tool.editingEnabled = true;
    tool.brush.sampleTargetHeight = true;
    switch (selected) {
    case EditorTerrainTool::Paint: tool.mode = EditorTerrainToolMode::Paint; return true;
    case EditorTerrainTool::Holes: tool.mode = EditorTerrainToolMode::Holes; tool.brush.mode = Mode::CutHole; return true;
    case EditorTerrainTool::River: tool.mode = EditorTerrainToolMode::River; tool.riverPendingVisible = false; return true;
    case EditorTerrainTool::Smooth: tool.brush.mode = Mode::Smooth; break;
    case EditorTerrainTool::Ramp: tool.brush.mode = Mode::Ramp; break;
    case EditorTerrainTool::Stamp: tool.brush.mode = Mode::Stamp; break;
    default: tool.brush.mode = Mode::Raise; break;
    }
    tool.mode = EditorTerrainToolMode::Sculpt;
    return true;
}

void EditorTerrainService::CycleToolVariant(EditorTerrainToolState& tool, std::size_t layerCount) noexcept {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (ActiveTool(tool)) {
    case EditorTerrainTool::Smooth:
        tool.brush.mode = tool.brush.mode == Mode::Smooth ? Mode::Flatten : Mode::Smooth;
        tool.brush.sampleTargetHeight = true;
        break;
    case EditorTerrainTool::Paint:
        // Layer 1 .. layer N, then Auto, then layer 1 again.
        if (tool.paintAuto) {
            tool.paintAuto = false;
            tool.selectedMaterialLayer = 0U;
        } else if (static_cast<std::size_t>(tool.selectedMaterialLayer) + 1U >= layerCount) {
            tool.paintAuto = true;
        } else {
            ++tool.selectedMaterialLayer;
        }
        break;
    case EditorTerrainTool::Stamp:
        tool.brush.stampBlend = static_cast<kb::terrain_editor::TerrainStampBlend>(
            (static_cast<std::uint8_t>(tool.brush.stampBlend) + 1U) % 3U);
        break;
    default: break;
    }
}

bool EditorTerrainService::LoadStampImage(EditorTerrainToolState& tool, const std::filesystem::path& path, std::string* error) {
    std::string reason;
    std::shared_ptr<const kb::terrain_editor::TerrainStampImage> image =
        kb::terrain_editor::TerrainHeightmapImporter::LoadStampImage(path, &reason);
    if (image == nullptr) {
        if (error != nullptr) *error = reason;
        return false;
    }
    tool.brush.stampImage = std::move(image);
    tool.stampImageName = path.filename().string();
    if (error != nullptr) error->clear();
    return true;
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
    for (const DefaultTerrainLayer& layer : kDefaultTerrainLayers) {
        const std::filesystem::path file = path.parent_path() / (std::string{ layer.name } + ".kbmat");
        std::error_code existsError;
        if (std::filesystem::exists(file, existsError)) continue;
        kb::render::RenderMaterialAssetData material{};
        material.desc.baseColor[0] = layer.color[0];
        material.desc.baseColor[1] = layer.color[1];
        material.desc.baseColor[2] = layer.color[2];
        material.desc.roughnessFactor = layer.roughness;
        if (!kb::render::RenderMaterialAssetWriter::Save(file, material)) {
            if (error != nullptr) *error = "Default terrain material could not be written: " + file.generic_string();
            return false;
        }
    }
    static_cast<void>(scene.Assets().Discover());
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset();
    for (const DefaultTerrainLayer& layer : kDefaultTerrainLayers) {
        const kb::assets::AssetMetadata* material = FindAssetByPath(scene, path.parent_path() / (std::string{ layer.name } + ".kbmat"));
        if (material == nullptr || !kb::terrain_editor::AddTerrainMaterialLayer(terrain, material->id.value)) {
            if (error != nullptr) *error = "Default terrain material was saved but asset discovery did not register it: " + std::string{ layer.name };
            return false;
        }
    }
    if (!kb::assets::TerrainAssetIO::Save(path, terrain, error)) return false;
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* metadata = FindAssetByPath(scene, path);
    if (metadata == nullptr) {
        if (error != nullptr) *error = "Terrain asset was saved but asset discovery did not register it";
        return false;
    }
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = metadata->id.value,
        .castsShadow = true,
        .receivesShadow = true,
    });
    AttachCollider(scene, entity);
    return true;
}

bool EditorTerrainService::Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    if (!IsTerrainEntity(scene, entity)) return false;
    const kb::scene::ColliderComponent* collider = scene.Components().Colliders().TryGet(entity);
    if (collider != nullptr && collider->shape == kb::scene::ColliderShape::Terrain) {
        scene.Components().Colliders().Remove(entity);
    }
    scene.Components().MeshRenderers().Remove(entity);
    return true;
}

void EditorTerrainService::AttachCollider(kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) return;
    kb::scene::ColliderComponent collider{};
    if (const kb::scene::ColliderComponent* existing = scene.Components().Colliders().TryGet(entity)) collider = *existing;
    collider.shape = kb::scene::ColliderShape::Terrain;
    collider.meshAssetId = renderer->meshAssetId;
    collider.center = {};
    scene.Components().Colliders().Set(entity, collider);
}

void EditorTerrainService::UpdateCollision(
    kb::scene::Scene& scene,
    kb::assets::AssetId assetId,
    const kb::assets::TerrainAsset& terrain,
    const kb::terrain_editor::TerrainBrushResult& changedRegion,
    bool holeCells) {
    const std::uint32_t reach = holeCells ? 1U : 0U;
    static_cast<void>(kb::scene::PhysicsBackend::UpdateTerrain(scene, assetId.value, terrain, kb::assets::TerrainSampleRegion{
        changedRegion.minX, changedRegion.minZ,
        std::min(changedRegion.maxX + reach, terrain.width - 1U), std::min(changedRegion.maxZ + reach, terrain.height - 1U) }));
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
    imported->lodPixelError = current.lodPixelError;
    imported->collisionStep = current.collisionStep;
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
    result.lodPixelError = current.lodPixelError;
    result.collisionStep = current.collisionStep;
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
