// Level of detail of a terrain mesh chosen per chunk: sections and errors the builder writes, gaps between chunks at
// different levels, and triangles and time of the scene pipeline as the camera moves away.
#include "RendererTestSupport.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "resources/RenderMeshResourceBuilder.hpp"
#include "scene/pipeline/MeshPipelineTerrainLod.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint32_t kLevelMask = 0x7FU;

[[nodiscard]] double Milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Rolling hills with small bumps: coarse levels stray from it by a measurable amount.
[[nodiscard]] float HillHeight(float x, float z) {
    const float bump = std::sin(x * 1.3F) * std::cos(z * 1.7F);
    return 9.0F * std::sin(x * 0.045F) * std::cos(z * 0.037F) + 2.5F * std::sin(x * 0.21F + z * 0.17F) + 0.35F * bump;
}

[[nodiscard]] kb::assets::TerrainAsset MakeHillTerrain(
    std::uint32_t resolution, float size, std::uint32_t chunkQuads, bool layers, float lodPixelError) {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(resolution, size, size);
    terrain.chunkQuads = chunkQuads;
    terrain.lodCount = 4U;
    terrain.lodPixelError = lodPixelError;
    const float cell = size / static_cast<float>(resolution - 1U);
    for (std::uint32_t z = 0U; z < resolution; ++z) {
        for (std::uint32_t x = 0U; x < resolution; ++x) {
            terrain.heights[static_cast<std::size_t>(z) * resolution + x] =
                HillHeight(static_cast<float>(x) * cell, static_cast<float>(z) * cell);
        }
    }
    if (layers) {
        terrain.materialLayers = { kb::assets::TerrainMaterialLayer{ 101U }, kb::assets::TerrainMaterialLayer{ 202U } };
        terrain.layerWeightWidth = 33U;
        terrain.layerWeightHeight = 33U;
        terrain.layerWeights.assign(33U * 33U * 4U, 0U);
        for (std::uint32_t texel = 0U; texel < 33U * 33U; ++texel) {
            const bool rock = (texel % 33U) > 20U;
            terrain.layerWeights[texel * 4U] = rock ? 0U : 255U;
            terrain.layerWeights[texel * 4U + 1U] = rock ? 255U : 0U;
        }
    }
    return terrain;
}

struct TerrainFixture {
    kb::assets::TerrainAsset terrain;
    RenderMeshAssetData asset;
    RenderMeshResource resource;
};

[[nodiscard]] TerrainFixture MakeFixture(
    std::uint32_t resolution, float size, std::uint32_t chunkQuads, bool layers, float lodPixelError = 2.0F) {
    TerrainFixture fixture{ MakeHillTerrain(resolution, size, chunkQuads, layers, lodPixelError), {}, {} };
    auto built = RenderTerrainMeshBuilder::Build(fixture.terrain);
    Require(built.has_value(), "The terrain mesh could not be built");
    fixture.asset = std::move(*built);
    fixture.resource = RenderMeshResourceBuilder::Build(
        fixture.asset.desc, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE);
    return fixture;
}

[[nodiscard]] std::uint32_t IndexAt(const RenderMeshAssetData& asset, std::size_t index) {
    return asset.indices16.empty() ? asset.indices32[index] : asset.indices16[index];
}

// A camera above the terrain looking straight down (the view looks along +z, as the renderer's projection does).
[[nodiscard]] SceneRenderCamera CameraAbove(float x, float height, float z, float fovDegrees, std::uint32_t viewportHeight) {
    SceneRenderCamera camera;
    camera.view = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, -x, -z, height, 1.0F };
    SceneDepthPolicy::MakePerspective(camera.projection.data(), fovDegrees, 16.0F / 9.0F, 0.1F, 100000.0F, SceneDepthPolicy::HomogeneousDepth());
    camera.viewportHeight = viewportHeight;
    return camera;
}

// A camera on the ground at the west side of the terrain, looking east over it.
[[nodiscard]] SceneRenderCamera CameraLookingEast(float x, float height, float z, float fovDegrees, std::uint32_t viewportHeight) {
    SceneRenderCamera camera;
    camera.view = { 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, z, -height, -x, 1.0F };
    SceneDepthPolicy::MakePerspective(camera.projection.data(), fovDegrees, 16.0F / 9.0F, 0.1F, 100000.0F, SceneDepthPolicy::HomogeneousDepth());
    camera.viewportHeight = viewportHeight;
    return camera;
}

[[nodiscard]] std::array<float, 16> Identity() {
    return { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
}

struct PassResult {
    std::uint64_t triangles = 0U;
    std::uint32_t draws = 0U;
    double milliseconds = 0.0;
};

[[nodiscard]] PassResult RunPass(const TerrainFixture& fixture, const SceneRenderCamera& camera, MeshPassType pass, std::uint32_t repeat) {
    const std::vector<SceneRenderDrawGroup> groups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = Identity() } },
        },
    };
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Opaque;
    const MeshPipelineBuildDesc desc{
        .pass = pass,
        .drawGroups = &groups,
        .resolvedMeshResource = &fixture.resource,
        .resolvedMaterialResource = &material,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    };
    PassResult result;
    MeshPipelineBuildResult scratch;
    double best = 1.0e30;
    for (std::uint32_t run = 0U; run < repeat; ++run) {
        const auto start = Clock::now();
        MeshPipelineProcessor::BuildInto(desc, scratch);
        best = std::min(best, Milliseconds(Clock::now() - start));
    }
    for (const MeshDrawCommand& command : scratch.commands) {
        result.triangles += static_cast<std::uint64_t>(command.indexCount / 3U) * command.instances.size();
        result.draws += static_cast<std::uint32_t>(command.instances.size());
    }
    result.milliseconds = best;
    return result;
}

void RunBuiltGridTest() {
    const TerrainFixture fixture = MakeFixture(129U, 128.0F, 16U, true);
    Require(fixture.asset.terrainChunks != nullptr, "The terrain mesh must hold a chunk grid");
    const RenderTerrainChunkGrid& grid = *fixture.asset.terrainChunks;
    Require(grid.Valid() && grid.chunkCountX == 8U && grid.chunkCountZ == 8U && grid.lodCount == 4U,
        "The terrain mesh did not describe its chunks");
    Require(fixture.resource.terrainChunks.Valid() && fixture.resource.lods.size() <= 1U,
        "The mesh resource must keep the chunk grid and drop the whole-mesh level table");
    for (std::uint32_t chunk = 0U; chunk < grid.ChunkCount(); ++chunk) {
        Require(grid.lodError[chunk * grid.lodCount] == 0.0F, "A chunk drawn at full resolution must show no error");
        for (std::uint32_t lod = 1U; lod < grid.lodCount; ++lod) {
            Require(grid.lodError[chunk * grid.lodCount + lod] >= grid.lodError[chunk * grid.lodCount + lod - 1U],
                "The error of a chunk must not shrink as its level gets coarser");
        }
        Require(grid.skirtDepth[chunk] > 0.0F, "Rolling hills must give every chunk a skirt");
        for (std::uint32_t lod = 0U; lod < grid.lodCount; ++lod) {
            const std::size_t task = static_cast<std::size_t>(lod) * grid.ChunkCount() + chunk;
            const std::uint32_t first = grid.sectionIndices[task];
            Require(first != UINT32_MAX && fixture.resource.sections[first].terrainChunk == chunk &&
                    fixture.resource.sections[first + 1U].terrainLayerIndex == 1U &&
                    fixture.resource.sections[first].lodLevel == lod && !fixture.resource.sections[first].terrainSkirt,
                "A chunk level must hold one section per material layer");
            const std::uint32_t skirt = grid.skirtSectionIndices[task];
            const bool hasNeighbour = grid.ChunkCount() > 1U;
            Require(!hasNeighbour || (skirt != UINT32_MAX && fixture.resource.sections[skirt].terrainSkirt &&
                        fixture.resource.sections[skirt].terrainChunk == chunk),
                "A chunk with neighbours must have a skirt section at every level");
        }
    }
    // Indices stay inside the vertex buffer, grid and skirt vertices alike.
    const std::size_t indexCount = fixture.asset.indices16.empty() ? fixture.asset.indices32.size() : fixture.asset.indices16.size();
    for (std::size_t index = 0U; index < indexCount; ++index) {
        Require(IndexAt(fixture.asset, index) < fixture.asset.tangentVertices.size(), "A terrain index points outside its vertices");
    }
}

struct EdgeLine {
    std::map<float, float> top;
    std::map<float, float> bottom;
};

[[nodiscard]] float At(const std::map<float, float>& line, float position) {
    Require(!line.empty(), "An edge line is empty");
    auto upper = line.lower_bound(position);
    if (upper == line.begin()) return upper->second;
    if (upper == line.end()) return std::prev(upper)->second;
    const auto lower = std::prev(upper);
    const float span = upper->first - lower->first;
    const float t = span > 0.0F ? (position - lower->first) / span : 0.0F;
    return lower->second + t * (upper->second - lower->second);
}

enum class Side { East, North };

// The edge of a chunk at a level as its skirt draws it: the line of its border and the line of the bottom of its skirt,
// along z for the east edge and along x for the north edge.
[[nodiscard]] EdgeLine EdgeOf(const TerrainFixture& fixture, std::uint32_t chunk, std::uint32_t lod, Side side, float edge) {
    const RenderTerrainChunkGrid& grid = *fixture.asset.terrainChunks;
    const std::uint32_t gridVertices = fixture.terrain.width * fixture.terrain.height;
    const RenderMeshSectionDesc& section = fixture.asset.sections[grid.skirtSectionIndices[static_cast<std::size_t>(lod) * grid.ChunkCount() + chunk]];
    EdgeLine line;
    for (std::uint32_t offset = 0U; offset < section.indexCount; offset += 3U) {
        std::array<const RenderStaticMeshVertexP3N3T4UV2*, 3U> corner{};
        bool onEdge = true;
        for (std::uint32_t k = 0U; k < 3U; ++k) {
            const std::uint32_t index = IndexAt(fixture.asset, section.indexStart + offset + k);
            corner[k] = &fixture.asset.tangentVertices[index];
            onEdge = onEdge && (side == Side::East ? corner[k]->x : corner[k]->z) == edge;
        }
        if (!onEdge) continue;
        for (std::uint32_t k = 0U; k < 3U; ++k) {
            const std::uint32_t index = IndexAt(fixture.asset, section.indexStart + offset + k);
            const float along = side == Side::East ? corner[k]->z : corner[k]->x;
            (index < gridVertices ? line.top : line.bottom)[along] = corner[k]->y;
        }
    }
    return line;
}

// Whether the walls of the chunks cover the gap between their borders along the whole shared edge.
[[nodiscard]] bool GapIsClosed(
    const TerrainFixture& fixture, std::uint32_t chunkA, std::uint32_t lodA, std::uint32_t chunkB, std::uint32_t lodB, Side side) {
    const std::uint32_t quadsX = fixture.terrain.width - 1U;
    const std::uint32_t chunkQuads = fixture.terrain.chunkQuads;
    const float cell = fixture.terrain.worldSizeX / static_cast<float>(quadsX);
    const std::uint32_t chunkX = chunkA % fixture.asset.terrainChunks->chunkCountX;
    const std::uint32_t chunkZ = chunkA / fixture.asset.terrainChunks->chunkCountX;
    const float half = fixture.terrain.worldSizeX * 0.5F;
    const float edge = side == Side::East ? static_cast<float>((chunkX + 1U) * chunkQuads) * cell - half
                                          : static_cast<float>((chunkZ + 1U) * chunkQuads) * cell - half;
    const float start = side == Side::East ? static_cast<float>(chunkZ * chunkQuads) * cell - half
                                           : static_cast<float>(chunkX * chunkQuads) * cell - half;
    // Chunk B lies beyond the edge, so the edge is its low side: its west or south edge has the same coordinate.
    const EdgeLine a = EdgeOf(fixture, chunkA, lodA, side, edge);
    const EdgeLine b = EdgeOf(fixture, chunkB, lodB, side, edge);
    for (std::uint32_t step = 0U; step <= chunkQuads; ++step) {
        const float position = start + static_cast<float>(step) * cell;
        const float topA = At(a.top, position);
        const float topB = At(b.top, position);
        if (lodA == lodB) {
            if (std::abs(topA - topB) > 1.0e-5F) return false;
            continue;
        }
        const bool aHigher = topA >= topB;
        const float bottom = aHigher ? At(a.bottom, position) : At(b.bottom, position);
        if (bottom > (aHigher ? topB : topA) + 1.0e-4F) return false;
    }
    return true;
}

void RunNoGapBetweenLevelsTest() {
    const TerrainFixture fixture = MakeFixture(129U, 128.0F, 16U, false);
    const RenderTerrainChunkGrid& grid = *fixture.asset.terrainChunks;
    std::uint32_t checkedPairs = 0U;
    // Every pair of levels for neighbours in both directions, over the whole terrain.
    for (std::uint32_t chunk = 0U; chunk < grid.ChunkCount(); ++chunk) {
        const std::uint32_t chunkX = chunk % grid.chunkCountX;
        const std::uint32_t chunkZ = chunk / grid.chunkCountX;
        for (std::uint32_t lodA = 0U; lodA < grid.lodCount; ++lodA) {
            for (std::uint32_t lodB = 0U; lodB < grid.lodCount; ++lodB) {
                if (chunkX + 1U < grid.chunkCountX) {
                    Require(GapIsClosed(fixture, chunk, lodA, chunk + 1U, lodB, Side::East),
                        "A gap is left between neighbouring chunks at different levels (east)");
                    ++checkedPairs;
                }
                if (chunkZ + 1U < grid.chunkCountZ) {
                    Require(GapIsClosed(fixture, chunk, lodA, chunk + grid.chunkCountX, lodB, Side::North),
                        "A gap is left between neighbouring chunks at different levels (north)");
                    ++checkedPairs;
                }
            }
        }
    }
    std::cout << "Terrain LOD seams: " << checkedPairs << " chunk pairs and level combinations closed\n";
}

// The levels the pipeline picks put a skirt on every chunk that has a neighbour at another level.
void RunSkirtsFollowLevelsTest() {
    const TerrainFixture fixture = MakeFixture(257U, 256.0F, 16U, false);
    const RenderTerrainChunkGrid& grid = *fixture.asset.terrainChunks;
    const SceneRenderMeshInstance instance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = Identity() };
    std::vector<std::uint8_t> levels;
    std::uint32_t transitions = 0U;
    const std::array<SceneRenderCamera, 4U> cameras{
        CameraLookingEast(0.0F, 3.0F, 128.0F, 75.0F, 1080U), CameraLookingEast(0.0F, 30.0F, 128.0F, 90.0F, 1080U),
        CameraAbove(128.0F, 70.0F, 128.0F, 100.0F, 1080U), CameraAbove(40.0F, 120.0F, 200.0F, 100.0F, 1080U),
    };
    for (const SceneRenderCamera& camera : cameras) {
        MeshPipelineTerrainLod::SelectChunkLevels(fixture.resource, std::span{ &instance, 1U }, &camera,
            MeshPipelineVisibility::BuildFrustum(&camera), levels);
        for (std::uint32_t chunk = 0U; chunk < grid.ChunkCount(); ++chunk) {
            if (levels[chunk] == MeshPipelineTerrainLod::kCulled) continue;
            const std::uint32_t lod = levels[chunk] & kLevelMask;
            const bool skirt = (levels[chunk] & MeshPipelineTerrainLod::kSkirt) != 0U;
            const std::uint32_t chunkX = chunk % grid.chunkCountX;
            const std::uint32_t chunkZ = chunk / grid.chunkCountX;
            bool neighbourDiffers = false;
            const auto visit = [&](std::uint32_t other) {
                // A neighbour out of view has no entry; only the visible ones are compared.
                if (levels[other] != MeshPipelineTerrainLod::kCulled && (levels[other] & kLevelMask) != lod) neighbourDiffers = true;
            };
            if (chunkX > 0U) visit(chunk - 1U);
            if (chunkX + 1U < grid.chunkCountX) visit(chunk + 1U);
            if (chunkZ > 0U) visit(chunk - grid.chunkCountX);
            if (chunkZ + 1U < grid.chunkCountZ) visit(chunk + grid.chunkCountX);
            Require(!neighbourDiffers || skirt, "A chunk beside one at another level has no skirt");
            transitions += skirt ? 1U : 0U;
        }
    }
    Require(transitions > 0U, "The camera positions must produce chunks at different levels");
}

// A shadow pass draws the surface the view it shades draws: with the level of detail view of that camera its chunks
// take the levels the view picks, where its own position would pick others.
void RunShadowFollowsViewLevelsTest() {
    const TerrainFixture fixture = MakeFixture(257U, 256.0F, 16U, false);
    const SceneRenderMeshInstance instance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = Identity() };
    const SceneRenderCamera view = CameraLookingEast(0.0F, 3.0F, 128.0F, 75.0F, 1080U);
    SceneRenderCamera shadow = CameraAbove(128.0F, 400.0F, 128.0F, 100.0F, 2048U);
    std::vector<std::uint8_t> viewLevels;
    std::vector<std::uint8_t> ownLevels;
    std::vector<std::uint8_t> followedLevels;
    const auto select = [&](const SceneRenderCamera& camera, std::vector<std::uint8_t>& levels) {
        MeshPipelineTerrainLod::SelectChunkLevels(fixture.resource, std::span{ &instance, 1U }, &camera,
            MeshPipelineVisibility::BuildFrustum(&camera), levels);
    };
    select(view, viewLevels);
    select(shadow, ownLevels);
    shadow.levelOfDetailView = MeshPipelineVisibility::LevelOfDetailViewOf(view);
    select(shadow, followedLevels);
    std::uint32_t compared = 0U;
    bool differsOwn = false;
    for (std::size_t chunk = 0U; chunk < viewLevels.size(); ++chunk) {
        if (viewLevels[chunk] == MeshPipelineTerrainLod::kCulled || followedLevels[chunk] == MeshPipelineTerrainLod::kCulled) continue;
        // The skirt flag follows the neighbours in view, which the two cameras see differently; the level must agree.
        Require((viewLevels[chunk] & kLevelMask) == (followedLevels[chunk] & kLevelMask),
            "A shadow pass did not take the level of detail of the view it shades");
        differsOwn = differsOwn || (ownLevels[chunk] != MeshPipelineTerrainLod::kCulled && (ownLevels[chunk] & kLevelMask) != (viewLevels[chunk] & kLevelMask));
        ++compared;
    }
    Require(compared > 8U && differsOwn, "The shadow camera must see chunks the view sees, at levels its own position would not pick");
}

// A brush changes the heights: the mesh resource must receive the new vertices, the new level errors and skirt
// depths, and the new bounds of the chunks.
void RunRegistryFollowsBrushTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    terrain.chunkQuads = 16U;
    std::optional<RenderMeshAssetData> built = RenderTerrainMeshBuilder::Build(terrain);
    Require(built.has_value() && RenderTerrainMeshBuilder::PrepareDynamicPreview(terrain, *built), "A dynamic terrain mesh could not be built");
    RenderMeshAssetData& asset = *built;
    RenderResourceRegistry registry;
    const RenderMeshHandle handle = registry.RegisterMesh(asset.desc);
    Require(handle.IsValid(), "The terrain mesh did not register");
    const std::size_t firstUpdate = asset.dynamicVertexUpdateRanges.size();
    const std::size_t firstSection = asset.dynamicSectionUpdateIndices.size();
    // A hill of 6 units over the middle of the terrain.
    std::uint32_t minX = 40U;
    std::uint32_t maxX = 90U;
    for (std::uint32_t z = minX; z <= maxX; ++z) {
        for (std::uint32_t x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - 65.0F;
            const float dz = static_cast<float>(z) - 65.0F;
            terrain.heights[static_cast<std::size_t>(z) * terrain.width + x] = 6.0F * std::exp(-(dx * dx + dz * dz) / 120.0F);
        }
    }
    Require(RenderTerrainMeshBuilder::UpdateDynamicPreview(terrain,
            RenderTerrainMeshUpdateRegion{ .minX = minX, .minZ = minX, .maxX = maxX, .maxZ = maxX }, asset),
        "The dynamic terrain mesh rejected the brush");
    for (std::size_t range = firstUpdate; range < asset.dynamicVertexUpdateRanges.size(); ++range) {
        Require(registry.UpdateMeshVertices(handle, asset.desc, asset.dynamicVertexUpdateRanges[range].firstVertex,
                asset.dynamicVertexUpdateRanges[range].vertexCount),
            "The mesh resource rejected a vertex update");
    }
    std::vector<std::uint32_t> sections(asset.dynamicSectionUpdateIndices.begin() + static_cast<std::ptrdiff_t>(firstSection),
        asset.dynamicSectionUpdateIndices.end());
    std::ranges::sort(sections);
    sections.erase(std::unique(sections.begin(), sections.end()), sections.end());
    Require(registry.UpdateMeshGeometryMetadata(handle, asset.desc, sections), "The mesh resource rejected the new geometry metadata");
    const RenderMeshResource* resource = registry.FindMesh(handle);
    Require(resource != nullptr && resource->terrainChunks.skirtDepth == asset.terrainChunks->skirtDepth &&
            resource->terrainChunks.lodError == asset.terrainChunks->lodError &&
            resource->terrainChunks.borderError == asset.terrainChunks->borderError,
        "The mesh resource kept the level errors of the terrain before the brush");
    const RenderTerrainChunkGrid& grid = *asset.terrainChunks;
    const std::uint32_t hillChunk = 4U * grid.chunkCountX + 4U;
    Require(grid.lodError[hillChunk * grid.lodCount + grid.lodCount - 1U] > 0.5F, "A hill must make its chunk stray from a coarse level");
    const RenderMeshSection& section = resource->sections[grid.sectionIndices[hillChunk]];
    Require(section.bounds.center[1] + section.bounds.radius > 5.0F, "The bounds of a chunk did not grow with the hill");
}

void RunTrianglesFallWithDistanceTest() {
    // A strict error budget makes the level follow the distance across the whole range of the test.
    const TerrainFixture fixture = MakeFixture(513U, 512.0F, 32U, true, 0.25F);
    std::uint64_t previous = UINT64_MAX;
    std::uint64_t first = 0U;
    for (const float height : { 300.0F, 500.0F, 900.0F, 1800.0F, 3600.0F }) {
        const SceneRenderCamera camera = CameraAbove(256.0F, height, 256.0F, 120.0F, 1080U);
        const PassResult pass = RunPass(fixture, camera, MeshPassType::BaseOpaque, 1U);
        Require(pass.triangles > 0U && pass.triangles <= previous,
            "The triangles of a terrain in view must not grow as the camera moves away");
        if (first == 0U) first = pass.triangles;
        previous = pass.triangles;
    }
    Require(previous * 3U <= first, "A terrain seen from far away must draw far fewer triangles than one seen from near");
}

// The level of the whole mesh, as the mesh table picks it: the sections of one level, those in view.
[[nodiscard]] std::uint64_t WholeMeshLevelTriangles(const TerrainFixture& fixture, const SceneRenderCamera& camera) {
    const RenderBoundsSphere bounds = fixture.resource.bounds;
    const float coverage = MeshPipelineVisibility::ScreenCoverage(&camera, bounds);
    constexpr std::array<float, 4U> thresholds{ 0.18F, 0.055F, 0.015F, 0.0F };
    std::uint32_t level = 3U;
    for (std::uint32_t candidate = 0U; candidate < 4U; ++candidate) {
        if (coverage >= thresholds[candidate]) {
            level = candidate;
            break;
        }
    }
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(&camera);
    const RenderTerrainChunkGrid& grid = fixture.resource.terrainChunks;
    std::uint64_t triangles = 0U;
    for (std::uint32_t chunk = 0U; chunk < grid.ChunkCount(); ++chunk) {
        const std::uint32_t first = grid.sectionIndices[static_cast<std::size_t>(level) * grid.ChunkCount() + chunk];
        if (first == UINT32_MAX) continue;
        const RenderMeshSection& section = fixture.resource.sections[first];
        if (MeshPipelineVisibility::IsInsideFrustum(frustum, section.bounds)) triangles += section.indexCount / 3U;
    }
    return triangles;
}

void RunLodBenchmark(std::uint32_t resolution, float size) {
    const auto buildStart = Clock::now();
    const TerrainFixture fixture = MakeFixture(resolution, size, 32U, true);
    const double buildMilliseconds = Milliseconds(Clock::now() - buildStart);
    const RenderTerrainChunkGrid& grid = *fixture.asset.terrainChunks;
    std::uint64_t fullTriangles = 0U;
    for (std::uint32_t chunk = 0U; chunk < grid.ChunkCount(); ++chunk) {
        const std::uint32_t first = grid.sectionIndices[chunk];
        if (first != UINT32_MAX) fullTriangles += fixture.resource.sections[first].indexCount / 3U;
    }
    std::cout << "Terrain " << resolution << "x" << resolution << " (" << grid.ChunkCount() << " chunks, 2 layers): mesh build "
              << buildMilliseconds << " ms, " << fullTriangles << " triangles at full resolution, "
              << fixture.asset.vertices.size() << " vertices\n";
    // A player on foot at the west edge looking over the terrain, a flyover, and the whole terrain in view from above.
    for (const float height : { 2.0F, 60.0F, 250.0F, size * 0.9F }) {
        const SceneRenderCamera camera = height < 10.0F
            ? CameraLookingEast(0.0F, height + HillHeight(0.0F, size * 0.5F), size * 0.5F, 75.0F, 1080U)
            : CameraAbove(size * 0.5F, height, size * 0.5F, 100.0F, 1080U);
        const PassResult pass = RunPass(fixture, camera, MeshPassType::BaseOpaque, 5U);
        const PassResult shadow = RunPass(fixture, camera, MeshPassType::Depth, 5U);
        const PassResult overlay = RunPass(fixture, camera, MeshPassType::BaseTransparent, 5U);
        std::cout << "  camera " << (height < 10.0F ? "on foot, looking over" : "above at") << " " << height << " m: chunk levels " << pass.triangles << " triangles in " << pass.draws
                  << " draws, build " << pass.milliseconds << " ms (depth pass " << shadow.milliseconds
                  << " ms); layer overlays " << overlay.triangles << " triangles in " << overlay.draws
                  << " draws; whole-mesh level " << WholeMeshLevelTriangles(fixture, camera) << " triangles\n";
    }
}

} // namespace

void RunTerrainLodTests() {
    RunBuiltGridTest();
    RunNoGapBetweenLevelsTest();
    RunSkirtsFollowLevelsTest();
    RunShadowFollowsViewLevelsTest();
    RunRegistryFollowsBrushTest();
    RunTrianglesFallWithDistanceTest();
}

void RunTerrainLodBenchmark() {
    RunLodBenchmark(1025U, 1024.0F);
    RunLodBenchmark(2049U, 2048.0F);
}

} // namespace kb::render::tests

int main(int argc, char** argv) {
    // Meshes register against the no-op renderer, which creates buffers and keeps no frames.
    bgfx::Init renderer;
    renderer.type = bgfx::RendererType::Noop;
    if (!bgfx::init(renderer)) {
        std::cerr << "Terrain LOD tests failed: the no-op renderer did not start\n";
        return EXIT_FAILURE;
    }
    int exitCode = EXIT_SUCCESS;
    try {
        if (argc == 2 && std::string_view{ argv[1] } == "bench") {
            kb::render::tests::RunTerrainLodBenchmark();
        } else {
            kb::render::tests::RunTerrainLodTests();
        }
        std::cout << "Terrain LOD tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Terrain LOD tests failed: " << error.what() << '\n';
        exitCode = EXIT_FAILURE;
    }
    bgfx::shutdown();
    return exitCode;
}
