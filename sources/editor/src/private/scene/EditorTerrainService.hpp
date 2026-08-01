#pragma once

#include "TerrainBrush.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace kb::scene { class Scene; }
namespace kb::render { struct RenderMeshAssetData; }

namespace kb::editor {

enum class EditorTerrainToolMode : std::uint8_t {
    Select,
    Sculpt,
    Holes,
};

struct EditorTerrainToolState {
    kb::terrain_editor::TerrainBrushSettings brush{};
    kb::terrain_editor::TerrainHeightmapImportSettings heightmapImport{};
    bool editingEnabled = false;
    bool strokeActive = false;
    float lastStampX = 0.0F;
    float lastStampZ = 0.0F;
    EditorTerrainToolMode mode = EditorTerrainToolMode::Sculpt;
    bool brushMenuOpen = false;
    bool brushShapeMenuOpen = false;
    bool hoverVisible = false;
    std::uint64_t hoverEntityId = 0U;
    float hoverLocalX = 0.0F;
    float hoverLocalZ = 0.0F;
};

struct EditorTerrainAssetState {
    kb::assets::AssetId assetId{};
    kb::assets::TerrainAsset terrain{};
};

struct EditorTerrainConfiguration {
    std::uint32_t width = 129U;
    std::uint32_t height = 129U;
    std::uint32_t chunkQuads = 32U;
    std::uint32_t lodCount = 4U;
    float worldSizeX = 128.0F;
    float worldSizeZ = 128.0F;
};

class EditorTerrainService final {
public:
    EditorTerrainService() = delete;
    [[nodiscard]] static EditorTerrainToolState& ToolState() noexcept;

    [[nodiscard]] static bool IsTerrainEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static std::optional<kb::assets::TerrainAsset> Load(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<EditorTerrainAssetState> Capture(
        const kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::string* error = nullptr);
    [[nodiscard]] static bool Create(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const std::filesystem::path& assetsRoot,
        std::string* error = nullptr);
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static bool PublishPreview(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::TerrainAsset& terrain,
        std::string* error = nullptr);
    [[nodiscard]] static std::shared_ptr<kb::render::RenderMeshAssetData> CreatePreviewMesh(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::TerrainAsset& terrain,
        std::string* error = nullptr);
    [[nodiscard]] static bool UpdatePreviewMesh(
        const kb::assets::TerrainAsset& terrain,
        const kb::terrain_editor::TerrainBrushResult& changedRegion,
        bool topologyChanged,
        std::shared_ptr<kb::render::RenderMeshAssetData>& mesh,
        std::string* error = nullptr);
    [[nodiscard]] static bool PublishPreview(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::TerrainAsset& terrain,
        std::shared_ptr<kb::render::RenderMeshAssetData> mesh,
        bool initializeDynamicResource,
        std::string* error = nullptr);
    [[nodiscard]] static bool Persist(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::TerrainAsset& terrain,
        std::string* error = nullptr);
    [[nodiscard]] static bool Persist(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::TerrainAsset& terrain,
        std::shared_ptr<kb::render::RenderMeshAssetData> mesh,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<kb::assets::TerrainAsset> BuildHeightmapImport(
        const kb::assets::TerrainAsset& current,
        const std::filesystem::path& heightmapPath,
        const kb::terrain_editor::TerrainHeightmapImportSettings& settings,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<kb::assets::TerrainAsset> BuildReconfigured(
        const kb::assets::TerrainAsset& current,
        const EditorTerrainConfiguration& configuration,
        std::string* error = nullptr);
};

} // namespace kb::editor
