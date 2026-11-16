#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"
#include "TerrainLayerPainter.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/assets/TerrainFoliage.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor::tests {
namespace {

constexpr std::uint64_t kMeshId = 0x1234U;

// Plants per chunk, by chunk coordinate, for one layer.
std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> CountPerChunk(const kb::assets::TerrainAsset& terrain, std::uint32_t layer) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> counts;
    for (const kb::assets::TerrainFoliageChunk& chunk : kb::assets::BuildTerrainFoliage(terrain)) {
        if (chunk.layer == layer) counts[{ chunk.coord.x, chunk.coord.z }] = chunk.instances.size();
    }
    return counts;
}

std::size_t Total(const std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t>& counts) {
    std::size_t total = 0U;
    for (const auto& entry : counts) total += entry.second;
    return total;
}

kb::assets::TerrainAsset TerrainWithLayer() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId), "A foliage layer should be added to an empty terrain");
    return terrain;
}

// A stroke the way the foliage tool makes it: the bar's brush, applied through the painter.
kb::terrain_editor::TerrainLayerPaintResult Stroke(
    kb::assets::TerrainAsset& terrain, const EditorTerrainToolState& tool, bool erase, float fromX, float fromZ, float toX, float toZ) {
    return kb::terrain_editor::ApplyTerrainFoliagePaintSegment(
        terrain, EditorTerrainService::FoliagePaintSettings(tool, erase),
        kb::terrain_editor::TerrainBrushStamp{ .localX = fromX, .localZ = fromZ },
        kb::terrain_editor::TerrainBrushStamp{ .localX = toX, .localZ = toZ });
}

void AddingALayerChangesTheTerrainData() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    Require(terrain.foliageLayers.empty() && terrain.foliageDensity.empty(), "A new terrain has no foliage");
    Require(!EditorTerrainService::AddFoliageLayer(terrain, 0U), "A foliage layer needs a mesh");
    Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId, 0x77U), "The layer should be added");
    Require(terrain.foliageLayers.size() == 1U && terrain.foliageLayers[0].meshAssetId == kMeshId &&
        terrain.foliageLayers[0].materialAssetId == 0x77U, "The layer should keep the mesh and material it was given");
    Require(terrain.foliageMapWidth >= 2U && terrain.foliageDensity.size() ==
        static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight, "The first layer should create its density map");
    Require(std::ranges::all_of(terrain.foliageDensity, [](std::uint8_t value) { return value == 0U; }), "A new layer starts unpainted");
    Require(kb::assets::IsTerrainAssetValid(terrain), "A terrain with a new layer should be valid");
    for (std::size_t index = 1U; index < kb::assets::TerrainAsset::MaximumFoliageLayers; ++index) {
        Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId), "Layers up to the limit should be added");
    }
    Require(!EditorTerrainService::AddFoliageLayer(terrain, kMeshId), "The layer past the limit should be refused");
}

void PanelFieldsReachTheData() {
    kb::assets::TerrainAsset terrain = TerrainWithLayer();
    kb::assets::TerrainFoliageLayer& layer = terrain.foliageLayers[0];
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::Density, 3.5F) && layer.density == 3.5F, "Density should reach the layer");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::Density, 1000.0F) && layer.density == 100.0F, "Density should stay in range");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::FadeStart, 120.0F) && layer.fadeStartDistance == 120.0F &&
        layer.cullDistance > layer.fadeStartDistance, "A fade distance past the cull distance should push the cull distance out");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::CullDistance, 300.0F) && layer.cullDistance == 300.0F, "Cull distance should reach the layer");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::CullDistance, 10.0F) && layer.cullDistance > layer.fadeStartDistance,
        "A cull distance below the fade distance should be lifted");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::MinScale, 2.0F) && layer.maxScale >= layer.minScale, "A larger minimum scale should lift the maximum");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::Rotation, 90.0F) && layer.rotationRangeDegrees == 90.0F, "Rotation should reach the layer");
    Require(EditorTerrainService::SetFoliageField(layer, EditorFoliageField::AlignToSlope, 2.0F) && layer.alignToNormal == 1.0F, "Align to slope should stay in range");
    Require(!EditorTerrainService::SetFoliageField(layer, EditorFoliageField::Density, std::numeric_limits<float>::quiet_NaN()), "A number that is not finite is refused");
    Require(EditorTerrainService::FoliageFieldValue(layer, EditorFoliageField::Rotation) == 90.0F, "The panel should read back what was set");
    Require(kb::assets::IsTerrainAssetValid(terrain), "Whatever the panel sets keeps the terrain valid");
}

void BrushRaisesInstancesUnderTheBrushOnly() {
    kb::assets::TerrainAsset terrain = TerrainWithLayer();
    terrain.foliageLayers[0].density = 4.0F;
    Require(Total(CountPerChunk(terrain, 0U)) == 0U, "An unpainted layer places no plants");
    EditorTerrainToolState tool{};
    tool.selectedFoliageLayer = 0U;
    tool.brush.radius = 6.0F;
    tool.brush.strength = 1.0F;
    const kb::terrain_editor::TerrainLayerPaintResult result = Stroke(terrain, tool, false, -50.0F, -50.0F, -44.0F, -50.0F);
    Require(result.Changed(), "The stroke should change the density map");
    const auto painted = CountPerChunk(terrain, 0U);
    Require(!painted.empty() && painted.contains({ 0U, 0U }) && painted.at({ 0U, 0U }) > 0U, "The chunk under the brush should get plants");
    Require(Total(painted) == painted.at({ 0U, 0U }), "The chunks away from the brush should stay empty");
}

void EraseLowersInstances() {
    kb::assets::TerrainAsset terrain = TerrainWithLayer();
    terrain.foliageLayers[0].density = 4.0F;
    EditorTerrainToolState tool{};
    tool.brush.radius = 8.0F;
    tool.brush.strength = 1.0F;
    static_cast<void>(Stroke(terrain, tool, false, -40.0F, -40.0F, -30.0F, -40.0F));
    const std::size_t painted = Total(CountPerChunk(terrain, 0U));
    Require(painted > 0U, "The stroke should place plants");
    Require(EditorTerrainService::FoliagePaintSettings(tool, true).mode == kb::terrain_editor::TerrainFoliagePaintMode::Erase, "Ctrl should erase");
    tool.brush.radius = 4.0F;
    static_cast<void>(Stroke(terrain, tool, true, -35.0F, -40.0F, -35.0F, -40.0F));
    const std::size_t after = Total(CountPerChunk(terrain, 0U));
    Require(after < painted, "Erase should lower the number of plants");
}

void SavedTerrainComesBackBitForBit() {
    // Undo saves the terrain it captured before the stroke; it must read back exactly as it was.
    kb::assets::TerrainAsset terrain = TerrainWithLayer();
    EditorTerrainToolState tool{};
    tool.brush.radius = 10.0F;
    static_cast<void>(Stroke(terrain, tool, false, 10.0F, 10.0F, 30.0F, 20.0F));
    const kb::assets::TerrainAsset before = terrain;
    static_cast<void>(Stroke(terrain, tool, false, -30.0F, -30.0F, -10.0F, -20.0F));
    Require(terrain.foliageDensity != before.foliageDensity, "The second stroke should change the map");

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "kb_editor_foliage_undo";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "Terrain.kbterrain";
    std::string error;
    Require(kb::assets::TerrainAssetIO::Save(path, terrain, &error), "The painted terrain should be saved");
    Require(kb::assets::TerrainAssetIO::Save(path, before, &error), "The captured terrain should be saved again");
    const std::optional<kb::assets::TerrainAsset> restored = kb::assets::TerrainAssetIO::Load(path, &error);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    Require(restored.has_value(), "The captured terrain should load");
    Require(restored->foliageDensity == before.foliageDensity && restored->foliageMapWidth == before.foliageMapWidth &&
        restored->foliageMapHeight == before.foliageMapHeight && restored->foliageLayers.size() == before.foliageLayers.size(),
        "Undo should bring the foliage data back bit for bit");
}

void RemovingALayerRemovesItsDensityMap() {
    kb::assets::TerrainAsset terrain = TerrainWithLayer();
    Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId + 1U), "A second layer should be added");
    EditorTerrainToolState tool{};
    tool.selectedFoliageLayer = 1U;
    tool.brush.radius = 10.0F;
    static_cast<void>(Stroke(terrain, tool, false, 0.0F, 0.0F, 10.0F, 0.0F));
    const std::size_t plane = static_cast<std::size_t>(terrain.foliageMapWidth) * terrain.foliageMapHeight;
    const std::vector<std::uint8_t> second(terrain.foliageDensity.begin() + static_cast<std::ptrdiff_t>(plane), terrain.foliageDensity.end());
    Require(terrain.foliageDensity.size() == 2U * plane, "Each layer has its own density plane");
    Require(kb::terrain_editor::RemoveTerrainFoliageLayer(terrain, 0U), "The first layer should be removed");
    Require(terrain.foliageLayers.size() == 1U && terrain.foliageLayers[0].meshAssetId == kMeshId + 1U &&
        terrain.foliageDensity == second, "Removing a layer should take its plane and keep the other one");
    Require(kb::terrain_editor::RemoveTerrainFoliageLayer(terrain, 0U), "The last layer should be removed");
    Require(terrain.foliageDensity.empty() && terrain.foliageMapWidth == 0U && terrain.foliageMapHeight == 0U,
        "Removing the last layer should remove the density map");
    Require(kb::assets::IsTerrainAssetValid(terrain), "A terrain without foliage should be valid");
}

void FoliageToolFollowsTheLayers() {
    EditorTerrainToolState tool{};
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    std::string error;
    Require(!EditorTerrainService::SelectTool(tool, EditorTerrainTool::Foliage, &terrain, &error) && !error.empty(),
        "The foliage tool needs a layer");
    Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId) && EditorTerrainService::AddFoliageLayer(terrain, kMeshId + 1U), "Two layers should be added");
    tool.brush.strength = 5.0F;
    Require(EditorTerrainService::SelectTool(tool, EditorTerrainTool::Foliage, &terrain, &error), "The foliage tool should start with a layer");
    Require(EditorTerrainService::ActiveTool(tool) == EditorTerrainTool::Foliage && tool.mode == EditorTerrainToolMode::Foliage, "Foliage should be the active tool");
    Require(tool.brush.strength <= 1.0F, "The foliage brush strength is an opacity");
    EditorTerrainService::CycleToolVariant(tool, &terrain);
    Require(tool.selectedFoliageLayer == 1U, "The variant button should pick the next layer");
    EditorTerrainService::CycleToolVariant(tool, &terrain);
    Require(tool.selectedFoliageLayer == 0U, "The variant button should wrap to the first layer");
    Require(EditorTerrainService::FoliagePaintSettings(tool, false).layerIndex == 0U &&
        EditorTerrainService::FoliagePaintSettings(tool, false).radius == tool.brush.radius, "The brush should use the bar's size and layer");
    Require(EditorTerrainService::SelectTool(tool, EditorTerrainTool::Foliage, &terrain, &error) &&
        EditorTerrainService::ActiveTool(tool) == EditorTerrainTool::None, "Clicking the active tool switches it off");
}

void PreviewGetsOnlyThePaintedTexels() {
    // The largest terrain a file holds (2049 samples) spread over 4096 m: one stroke must not copy the terrain.
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(2049U, 4096.0F, 4096.0F);
    Require(EditorTerrainService::AddFoliageLayer(terrain, kMeshId), "A layer should be added to the large terrain");
    kb::render::RenderMeshAssetData mesh;
    EditorTerrainService::AttachFoliageSource(terrain, mesh); // what saving the terrain gives the mesh, once
    const kb::assets::TerrainAsset beforeStroke = terrain;
    const std::shared_ptr<const kb::assets::TerrainAsset> source = mesh.terrainFoliage;
    const std::uint64_t revision = mesh.terrainFoliageRevision;
    const std::uint64_t copiedBefore = EditorTerrainService::FoliagePreviewBytesCopied();

    EditorTerrainToolState tool{};
    tool.brush.radius = 6.0F;
    tool.brush.strength = 1.0F;
    const kb::terrain_editor::TerrainLayerPaintResult result = Stroke(terrain, tool, false, -2000.0F, -2000.0F, -1990.0F, -2000.0F);
    Require(result.Changed(), "The stroke should change the density map");
    EditorTerrainService::UpdateFoliagePreview(terrain, 0U, result, mesh);
    const std::uint64_t copied = EditorTerrainService::FoliagePreviewBytesCopied() - copiedBefore;

    // The data of the chunks under the brush: their density texels, with one texel of margin on each side.
    const float texel = terrain.worldSizeX / static_cast<float>(terrain.foliageMapWidth - 1U);
    const std::uint64_t side = static_cast<std::uint64_t>(std::ceil(kb::assets::kTerrainFoliageChunkMeters / texel)) + 2U;
    const auto chunkOf = [](float meters) { return static_cast<std::int64_t>(std::floor((meters + 2048.0F) / kb::assets::kTerrainFoliageChunkMeters)); };
    const std::uint64_t chunksX = static_cast<std::uint64_t>(chunkOf(-1990.0F + 6.0F) - chunkOf(-2000.0F - 6.0F) + 1);
    const std::uint64_t chunksZ = static_cast<std::uint64_t>(chunkOf(-2000.0F + 6.0F) - chunkOf(-2000.0F - 6.0F) + 1);
    Require(copied > 0U && copied <= chunksX * chunksZ * side * side, "The preview should get no more than the chunks under the brush");
    Require(copied * 1000U < beforeStroke.heights.size() * sizeof(float), "The preview must not get the terrain");
    Require(mesh.terrainFoliage == source && mesh.terrainFoliageRevision == revision + 1U, "The preview source is edited in place and flagged");
    Require(mesh.terrainFoliage->foliageDensity == terrain.foliageDensity, "The preview source should follow the painted map");
    Require(!kb::assets::BuildTerrainFoliage(*mesh.terrainFoliage).empty(), "The painted chunks should place plants");

    // Undo saves the terrain from before the stroke, and the mesh is made again from it.
    kb::render::RenderMeshAssetData restored;
    EditorTerrainService::AttachFoliageSource(beforeStroke, restored);
    Require(restored.terrainFoliage != nullptr && restored.terrainFoliage->foliageDensity == beforeStroke.foliageDensity &&
        kb::assets::BuildTerrainFoliage(*restored.terrainFoliage).empty(), "The preview after Undo should show the unpainted terrain");
}

} // namespace

void RunEditorTerrainFoliageTests() {
    AddingALayerChangesTheTerrainData();
    PanelFieldsReachTheData();
    BrushRaisesInstancesUnderTheBrushOnly();
    EraseLowersInstances();
    SavedTerrainComesBackBitForBit();
    RemovingALayerRemovesItsDensityMap();
    FoliageToolFollowsTheLayers();
    PreviewGetsOnlyThePaintedTexels();
}

} // namespace kb::editor::tests
