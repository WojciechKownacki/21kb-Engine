#include "TerrainBrush.hpp"
#include "TerrainHeightmapImporter.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::size_t Center(const kb::assets::TerrainAsset& terrain) {
    return static_cast<std::size_t>(terrain.height / 2U) * terrain.width + terrain.width / 2U;
}

void RunAssetRoundTripTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(33U, 64.0F, 48.0F);
    terrain.heights[Center(terrain)] = 12.5F;
    terrain.holes[0] = 1U;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kb_terrain_editor_roundtrip.kbterrain";
    std::string error;
    Require(kb::assets::TerrainAssetIO::Save(path, terrain, &error), error.c_str());
    const std::optional<kb::assets::TerrainAsset> loaded = kb::assets::TerrainAssetIO::Load(path, &error);
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    Require(loaded.has_value(), error.c_str());
    Require(loaded->width == 33U && loaded->height == 33U && loaded->worldSizeX == 64.0F && loaded->worldSizeZ == 48.0F,
        "Terrain round-trip changed header fields");
    Require(loaded->heights[Center(*loaded)] == 12.5F && loaded->holes[0] == 1U,
        "Terrain round-trip changed authored samples");
}

void RunBrushTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    kb::terrain_editor::TerrainBrushSettings brush{ .mode = kb::terrain_editor::TerrainBrushMode::Raise, .radius = 4.0F, .strength = 3.0F, .falloff = 0.5F };
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed(), "Raise brush did not change terrain");
    Require(std::abs(terrain.heights[Center(terrain)] - 3.0F) < 0.001F, "Raise brush strength was not deterministic");
    brush.mode = kb::terrain_editor::TerrainBrushMode::Lower;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed(), "Lower brush did not change terrain");
    Require(std::abs(terrain.heights[Center(terrain)]) < 0.001F, "Lower brush did not invert raise");
    brush.mode = kb::terrain_editor::TerrainBrushMode::Flatten;
    brush.targetHeight = 7.0F;
    brush.strength = 1.0F;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}));
    Require(std::abs(terrain.heights[Center(terrain)] - 7.0F) < 0.001F, "Flatten brush missed its target height");
    terrain.heights[Center(terrain)] = 9.0F;
    brush.mode = kb::terrain_editor::TerrainBrushMode::Smooth;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed() && terrain.heights[Center(terrain)] < 9.0F,
        "Smooth brush did not relax a height discontinuity");
    brush.mode = kb::terrain_editor::TerrainBrushMode::Noise;
    brush.noiseSeed = 42U;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed(), "Noise brush did not modify terrain");
    terrain.heights[Center(terrain)] = 3.25F;
    brush.mode = kb::terrain_editor::TerrainBrushMode::Terrace;
    brush.terraceStep = 2.0F;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed() &&
            std::abs(terrain.heights[Center(terrain)] - 4.0F) < 0.001F,
        "Terrace brush did not quantize the target height");
    brush.mode = kb::terrain_editor::TerrainBrushMode::CutHole;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed(), "Cut Hole brush did not change the hole mask");
    brush.mode = kb::terrain_editor::TerrainBrushMode::FillHole;
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, {}).Changed(), "Fill Hole brush did not restore the hole mask");
}

void RunBrushShapeTest() {
    kb::terrain_editor::TerrainBrushSettings brush{
        .mode = kb::terrain_editor::TerrainBrushMode::Raise,
        .shape = kb::terrain_editor::TerrainBrushShape::SoftRound,
        .radius = 6.0F,
        .strength = 1.0F,
        .falloff = 0.25F,
    };
    kb::assets::TerrainAsset soft = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    const kb::terrain_editor::TerrainBrushResult softResult =
        kb::terrain_editor::ApplyTerrainBrush(soft, brush, {});
    brush.shape = kb::terrain_editor::TerrainBrushShape::HardRound;
    kb::assets::TerrainAsset hard = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    const kb::terrain_editor::TerrainBrushResult hardResult =
        kb::terrain_editor::ApplyTerrainBrush(hard, brush, {});
    Require(hardResult.Changed() && softResult.Changed() &&
            hard.heights[Center(hard) + 5U] > soft.heights[Center(soft) + 5U] + 0.25F,
        "Hard Round brush did not preserve a stronger edge than Soft Round");

    brush.shape = kb::terrain_editor::TerrainBrushShape::Ring;
    kb::assets::TerrainAsset ring = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    Require(kb::terrain_editor::ApplyTerrainBrush(ring, brush, {}).Changed(),
        "Ring brush did not produce an annular footprint");
    Require(std::abs(ring.heights[Center(ring)]) < 0.001F,
        "Ring brush unexpectedly modified its empty center");

    brush.shape = kb::terrain_editor::TerrainBrushShape::Speckle;
    kb::assets::TerrainAsset speckleA = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    kb::assets::TerrainAsset speckleB = speckleA;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(speckleA, brush, {}));
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(speckleB, brush, {}));
    Require(speckleA.heights == speckleB.heights,
        "Speckle brush footprint is not deterministic");
}

void RunHeightmapImportTest() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kb_terrain_editor_heightmap.r16";
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        Require(output.is_open(), "Heightmap test could not create its RAW16 source");
        for (std::uint32_t index = 0U; index < 17U * 17U; ++index) {
            const std::uint16_t value = static_cast<std::uint16_t>((index * 65535U) / (17U * 17U - 1U));
            const char encoded[2]{
                static_cast<char>(value & 0xFFU),
                static_cast<char>((value >> 8U) & 0xFFU),
            };
            output.write(encoded, sizeof(encoded));
        }
        Require(output.good(), "Heightmap test could not write its RAW16 source");
    }
    std::string error;
    const std::optional<kb::assets::TerrainAsset> terrain =
        kb::terrain_editor::TerrainHeightmapImporter::Import(
            path,
            kb::terrain_editor::TerrainHeightmapImportSettings{
                .minimumHeight = -10.0F,
                .maximumHeight = 30.0F,
            },
            &error);
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    Require(terrain.has_value(), error.c_str());
    Require(terrain->width == 17U && terrain->height == 17U,
        "RAW16 heightmap import changed a valid terrain resolution");
    Require(std::abs(terrain->heights.front() + 10.0F) < 0.001F &&
            std::abs(terrain->heights.back() - 30.0F) < 0.001F,
        "RAW16 heightmap import did not preserve the configured height range");
}

void RunMeshBuildTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    terrain.chunkQuads = 16U;
    terrain.lodCount = 4U;
    const std::optional<kb::render::RenderMeshAssetData> mesh = kb::render::RenderTerrainMeshBuilder::Build(terrain);
    Require(mesh.has_value(), "Terrain mesh builder rejected a valid terrain");
    Require(mesh->lods.size() == 4U, "Terrain mesh did not preserve four LOD levels");
    Require(mesh->sections.size() == 64U, "Terrain mesh did not emit one section per chunk and LOD");
    Require(mesh->desc.gpuDriven.allowGpuCulling && mesh->meshlets.size() == mesh->sections.size(),
        "Terrain chunks are not represented in GPU-driven culling metadata");
    Require(mesh->embeddedMaterials.size() == 1U &&
            mesh->embeddedMaterials.front().desc.baseColor[0] == 1.0F &&
            mesh->embeddedMaterials.front().desc.baseColor[1] == 1.0F &&
            mesh->embeddedMaterials.front().desc.baseColor[2] == 1.0F,
        "Terrain mesh does not provide a neutral white default surface material");
    const std::size_t beforeIndices = mesh->desc.indexCount;
    terrain.holes[32U * 64U + 32U] = 1U;
    const std::optional<kb::render::RenderMeshAssetData> withHole = kb::render::RenderTerrainMeshBuilder::Build(terrain);
    Require(withHole.has_value() && withHole->desc.indexCount < beforeIndices, "Terrain hole did not remove rendered triangles");
    Require(withHole->dynamicTopologyKey != mesh->dynamicTopologyKey,
        "Terrain hole did not invalidate dynamic mesh topology identity");
}

void RunDynamicMeshUpdateTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    std::optional<kb::render::RenderMeshAssetData> mesh =
        kb::render::RenderTerrainMeshBuilder::Build(terrain);
    Require(mesh.has_value() &&
            kb::render::RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *mesh),
        "Terrain mesh could not enter dynamic preview mode");
    const std::uint64_t topologyKey = mesh->dynamicTopologyKey;
    const std::uint32_t fullVertexCount = mesh->desc.vertexCount;
    const float untouchedHeight = mesh->tangentVertices.front().y;
    const std::size_t initialUpdateRangeCount = mesh->dynamicVertexUpdateRanges.size();

    const kb::terrain_editor::TerrainBrushResult changed =
        kb::terrain_editor::ApplyTerrainBrush(
            terrain,
            kb::terrain_editor::TerrainBrushSettings{
                .mode = kb::terrain_editor::TerrainBrushMode::Raise,
                .radius = 3.0F,
                .strength = 1.0F,
            },
            {});
    Require(changed.Changed(), "Dynamic terrain update test brush changed no samples");
    Require(kb::render::RenderTerrainMeshBuilder::UpdateDynamicPreview(
            terrain,
            kb::render::RenderTerrainMeshUpdateRegion{
                .minX = changed.minX,
                .minZ = changed.minZ,
                .maxX = changed.maxX,
                .maxZ = changed.maxZ,
            },
            *mesh),
        "Terrain mesh rejected a height-only dynamic update");
    Require(mesh->desc.dynamicVertexBuffer && mesh->dynamicTopologyKey == topologyKey,
        "Dynamic terrain update changed GPU topology identity");
    Require(mesh->vertexUpdateCount > 0U && mesh->vertexUpdateCount < fullVertexCount / 4U,
        "Dynamic terrain update did not stay local to the brush region");
    std::uint32_t uploadedVertexCount = 0U;
    for (std::size_t rangeIndex = initialUpdateRangeCount;
         rangeIndex < mesh->dynamicVertexUpdateRanges.size();
         ++rangeIndex) {
        uploadedVertexCount += mesh->dynamicVertexUpdateRanges[rangeIndex].vertexCount;
    }
    Require(uploadedVertexCount == mesh->vertexUpdateCount &&
            mesh->dynamicVertexUpdateRanges.size() > initialUpdateRangeCount + 1U,
        "Dynamic terrain update did not split the brush into compact row uploads");
    Require(mesh->tangentVertices[Center(terrain)].y > 0.9F &&
            mesh->tangentVertices.front().y == untouchedHeight,
        "Dynamic terrain update changed the wrong vertex range");
}

void RunSmoothWorkspaceTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(513U, 512.0F, 512.0F);
    terrain.heights[Center(terrain)] = 10.0F;
    std::vector<float> scratchHeights;
    const kb::terrain_editor::TerrainBrushResult changed =
        kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(
            terrain,
            kb::terrain_editor::TerrainBrushSettings{
                .mode = kb::terrain_editor::TerrainBrushMode::Smooth,
                .radius = 4.0F,
                .strength = 1.0F,
            },
            {},
            scratchHeights);
    Require(changed.Changed() && terrain.heights[Center(terrain)] < 10.0F,
        "Smooth brush workspace path did not smooth the center sample");
    Require(scratchHeights.size() < 512U,
        "Smooth brush copied the full terrain instead of its local workspace");

    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t stampIndex = 0U; stampIndex < 200U; ++stampIndex) {
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(
            terrain,
            kb::terrain_editor::TerrainBrushSettings{
                .mode = kb::terrain_editor::TerrainBrushMode::Smooth,
                .radius = 4.0F,
                .strength = 0.25F,
            },
            kb::terrain_editor::TerrainBrushStamp{
                .localX = static_cast<float>(stampIndex % 16U) * 0.25F,
            },
            scratchHeights));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "Terrain smooth benchmark: 200 stamps="
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
              << " ms, scratch samples=" << scratchHeights.size() << '\n';
}

void RunDynamicMeshBenchmark() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(513U, 512.0F, 512.0F);
    std::optional<kb::render::RenderMeshAssetData> mesh =
        kb::render::RenderTerrainMeshBuilder::Build(terrain);
    Require(mesh.has_value() &&
            kb::render::RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *mesh),
        "Terrain benchmark could not prepare its dynamic mesh");
    const std::size_t initialUpdateRangeCount = mesh->dynamicVertexUpdateRanges.size();
    const kb::terrain_editor::TerrainBrushSettings brush{
        .mode = kb::terrain_editor::TerrainBrushMode::Raise,
        .radius = 4.0F,
        .strength = 0.05F,
    };
    const auto updateStart = std::chrono::steady_clock::now();
    for (std::uint32_t stampIndex = 0U; stampIndex < 200U; ++stampIndex) {
        const float offset = static_cast<float>(stampIndex % 25U) * 0.25F;
        const kb::terrain_editor::TerrainBrushResult changed =
            kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(
                terrain, brush,
                kb::terrain_editor::TerrainBrushStamp{
                    .localX = offset,
                    .localZ = offset * 0.5F,
                });
        Require(changed.Changed() &&
                kb::render::RenderTerrainMeshBuilder::UpdateDynamicPreview(
                    terrain,
                    kb::render::RenderTerrainMeshUpdateRegion{
                        .minX = changed.minX,
                        .minZ = changed.minZ,
                        .maxX = changed.maxX,
                        .maxZ = changed.maxZ,
                    },
                    *mesh),
            "Terrain benchmark dynamic stamp failed");
    }
    const auto updateElapsed = std::chrono::steady_clock::now() - updateStart;
    const auto rebuildStart = std::chrono::steady_clock::now();
    const std::optional<kb::render::RenderMeshAssetData> rebuilt =
        kb::render::RenderTerrainMeshBuilder::Build(terrain);
    const auto rebuildElapsed = std::chrono::steady_clock::now() - rebuildStart;
    Require(rebuilt.has_value(), "Terrain benchmark full rebuild failed");
    Require(updateElapsed < rebuildElapsed,
        "Two hundred local terrain stamps cost more than one full mesh rebuild");
    std::uint64_t uploadedVertexCount = 0U;
    for (std::size_t rangeIndex = initialUpdateRangeCount;
         rangeIndex < mesh->dynamicVertexUpdateRanges.size();
         ++rangeIndex) {
        uploadedVertexCount += mesh->dynamicVertexUpdateRanges[rangeIndex].vertexCount;
    }
    std::cout << "Terrain dynamic benchmark: 200 stamps="
              << std::chrono::duration_cast<std::chrono::milliseconds>(updateElapsed).count()
              << " ms, queued vertices=" << uploadedVertexCount
              << ", full rebuild="
              << std::chrono::duration_cast<std::chrono::milliseconds>(rebuildElapsed).count()
              << " ms\n";
}

void RunRuntimeLoaderTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kb_terrain_editor_loader";
    std::error_code filesystemError;
    std::filesystem::remove_all(root, filesystemError);
    filesystemError.clear();
    std::filesystem::create_directories(root, filesystemError);
    Require(!filesystemError, "Terrain loader test could not create its temporary asset root");

    const std::filesystem::path path = root / "Runtime.kbterrain";
    std::string error;
    Require(kb::assets::TerrainAssetIO::Save(path, kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F), &error), error.c_str());

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()),
        "AssetManager rejected the terrain render-mesh loader");
    Require(manager.Mounts().Mount("Game", root), "AssetManager could not mount the terrain test root");
    Require(manager.DiscoverMountedAssets() == 1U, "AssetManager did not discover the terrain asset");
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Runtime.kbterrain");
    Require(metadata != nullptr && metadata->type == "RenderMesh", "Terrain asset was registered with the wrong runtime type");
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> mesh =
        manager.Load<kb::render::RenderMeshAssetData>(metadata->id);
    Require(mesh.IsLoaded() && mesh->desc.vertexCount == 1089U && mesh->desc.indexCount != 0U,
        "Runtime terrain render mesh did not load through AssetManager");

    std::filesystem::remove_all(root, filesystemError);
}

} // namespace

int main() {
    try {
        RunAssetRoundTripTest();
        RunBrushTest();
        RunBrushShapeTest();
        RunHeightmapImportTest();
        RunMeshBuildTest();
        RunDynamicMeshUpdateTest();
        RunSmoothWorkspaceTest();
        RunDynamicMeshBenchmark();
        RunRuntimeLoaderTest();
        std::cout << "Terrain Editor tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Terrain Editor tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
