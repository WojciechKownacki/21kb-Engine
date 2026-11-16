#pragma once

#include "TerrainBrush.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "TerrainLayerPainter.hpp"
#include "TerrainPresets.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene { class Scene; }
namespace kb::render { struct RenderMeshAssetData; }

namespace kb::editor {

enum class EditorTerrainToolMode : std::uint8_t {
    Select,
    Sculpt,
    Holes,
    Paint,
    Foliage,
};

// The tools of the terrain bar. Holes (cut and fill) are reached through the operation menu of the
// Advanced drawer and name no button.
enum class EditorTerrainTool : std::uint8_t {
    None,
    Sculpt,
    Smooth, // Smooth, or Flatten to a height
    Paint,
    Foliage,
    Ramp,
    Stamp,
    Holes,
};

// What the strip under the terrain bar shows: the New World wizard or the Advanced settings.
enum class EditorTerrainDrawer : std::uint8_t {
    None,
    NewWorld,
    Advanced,
};

// The worlds the wizard offers: names of terrain_editor presets.
inline constexpr std::array<std::string_view, 4U> kEditorTerrainWorldPresets{ "island", "mountains", "plains", "valley" };

struct EditorTerrainToolState {
    // sampleTargetHeight: Flatten and Ramp take their heights from the ground where the stroke begins until
    // Alt picks one (see EditorTerrainViewportInteraction).
    kb::terrain_editor::TerrainBrushSettings brush{ .sampleTargetHeight = true };
    // The brush a stroke runs with, settled when it began (the heights it sampled, the Ctrl turn is added per stamp).
    kb::terrain_editor::TerrainBrushSettings strokeBrush{};
    EditorTerrainDrawer drawer = EditorTerrainDrawer::None;
    // Paint: choose the layer by the slope of the ground instead of painting the selected layer.
    bool paintAuto = false;
    // Colours the terrain by the angle of its slope.
    bool slopeOverlay = false;
    // A ramp stroke has begun: its start is lastStampX/Z, its end is where the button goes up.
    bool rampDragging = false;
    std::string stampImageName;
    std::uint8_t worldPreset = 0U;
    float worldCharacter = kb::terrain_editor::kTerrainDefaultCharacter;
    kb::terrain_editor::TerrainHeightmapImportSettings heightmapImport{};
    bool editingEnabled = false;
    bool strokeActive = false;
    float heldSculptElapsedSeconds = 0.0F;
    float lastStampX = 0.0F;
    float lastStampZ = 0.0F;
    EditorTerrainToolMode mode = EditorTerrainToolMode::Sculpt;
    std::uint8_t selectedMaterialLayer = 0U;
    std::uint8_t selectedFoliageLayer = 0U;
    bool brushMenuOpen = false;
    bool brushShapeMenuOpen = false;
    bool hoverVisible = false;
    std::uint64_t hoverEntityId = 0U;
    float hoverLocalX = 0.0F;
    float hoverLocalZ = 0.0F;
};

// The numbers of a foliage layer the Inspector edits one by one.
enum class EditorFoliageField : std::uint8_t {
    Density,
    MinScale,
    MaxScale,
    Rotation,
    AlignToSlope,
    FadeStart,
    CullDistance,
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

    // Which of the six tools is active, from the mode and the brush operation (None while selecting).
    [[nodiscard]] static EditorTerrainTool ActiveTool(const EditorTerrainToolState& tool) noexcept;
    // Makes `selected` the active tool; a tool that is already active is switched off (back to selecting).
    // False with the reason when the tool cannot work yet: Paint needs a material layer, Stamp a height image.
    [[nodiscard]] static bool SelectTool(
        EditorTerrainToolState& tool, EditorTerrainTool selected, const kb::assets::TerrainAsset* terrain, std::string* error);
    // The next variant of the active tool: Smooth <-> Flatten, the next paint layer then Auto, the next foliage
    // layer, the next stamp blend.
    static void CycleToolVariant(EditorTerrainToolState& tool, const kb::assets::TerrainAsset* terrain) noexcept;

    // Adds a foliage layer for `meshAssetId` with the default look; false when the terrain holds the most layers.
    [[nodiscard]] static bool AddFoliageLayer(
        kb::assets::TerrainAsset& terrain, std::uint64_t meshAssetId, std::uint64_t materialAssetId = 0U) noexcept;
    // Sets one number of a foliage layer, kept inside what a terrain accepts (a larger minimum scale lifts the
    // maximum, a fade distance past the cull distance pushes it out). False for a number that is not finite.
    [[nodiscard]] static bool SetFoliageField(
        kb::assets::TerrainFoliageLayer& layer, EditorFoliageField field, float value) noexcept;
    [[nodiscard]] static float FoliageFieldValue(const kb::assets::TerrainFoliageLayer& layer, EditorFoliageField field) noexcept;
    // The foliage brush from the bar: the same size, tip, falloff and seed as painting a material; Ctrl erases.
    [[nodiscard]] static kb::terrain_editor::TerrainFoliagePaintSettings FoliagePaintSettings(
        const EditorTerrainToolState& tool, bool erase) noexcept;
    // Points a preview mesh at a copy of the terrain its foliage is placed from (none when the terrain has no
    // foliage). The whole terrain is copied: used when a terrain is saved or rebuilt, not while painting.
    static void AttachFoliageSource(const kb::assets::TerrainAsset& terrain, kb::render::RenderMeshAssetData& mesh);
    // Brings the foliage source of a preview up to a painted region of `layerIndex`: only the changed density
    // texels are copied, in place, and the scene is told to place the plants of the chunks that changed again.
    static void UpdateFoliagePreview(
        const kb::assets::TerrainAsset& working, std::uint32_t layerIndex,
        const kb::terrain_editor::TerrainLayerPaintResult& changed, kb::render::RenderMeshAssetData& mesh);
    // Bytes of terrain data copied into foliage sources since the program started.
    [[nodiscard]] static std::uint64_t FoliagePreviewBytesCopied() noexcept;
    // Loads the height image the Stamp tool presses into the ground.
    [[nodiscard]] static bool LoadStampImage(EditorTerrainToolState& tool, const std::filesystem::path& path, std::string* error);

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
    [[nodiscard]] static std::shared_ptr<kb::render::RenderMeshAssetData> CreateLayerPreviewMesh(
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
    [[nodiscard]] static bool UpdateLayerPreviewMesh(
        const kb::assets::TerrainAsset& terrain,
        const kb::terrain_editor::TerrainLayerPaintResult& changedRegion,
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
        std::string* error = nullptr,
        bool foliageInSync = false); // true: the mesh's foliage source already follows `terrain` (a foliage stroke)
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
