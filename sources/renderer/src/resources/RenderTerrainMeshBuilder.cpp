#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] std::size_t VertexIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * terrain.width + x;
}

[[nodiscard]] std::size_t CellIndex(const kb::assets::TerrainAsset& terrain, std::uint32_t x, std::uint32_t z) noexcept {
    return static_cast<std::size_t>(z) * (terrain.width - 1U) + x;
}

[[nodiscard]] bool CoarseCellHasHole(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z,
    std::uint32_t step) noexcept {
    const std::uint32_t lastX = std::min(x + step, terrain.width - 1U);
    const std::uint32_t lastZ = std::min(z + step, terrain.height - 1U);
    for (std::uint32_t cellZ = z; cellZ < lastZ; ++cellZ) {
        for (std::uint32_t cellX = x; cellX < lastX; ++cellX) {
            if (terrain.holes[CellIndex(terrain, cellX, cellZ)] != 0U) return true;
        }
    }
    return false;
}

[[nodiscard]] bool TerrainChunkLayerHasWeight(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t chunkIndexX,
    std::uint32_t chunkIndexZ,
    std::uint32_t layer) noexcept {
    if (layer == 0U) return true;
    if (layer >= terrain.materialLayers.size() || terrain.layerWeightWidth < 2U ||
        terrain.layerWeightHeight < 2U) return false;
    const std::uint32_t terrainQuadsX = terrain.width - 1U;
    const std::uint32_t terrainQuadsZ = terrain.height - 1U;
    const std::uint32_t startTerrainX = chunkIndexX * terrain.chunkQuads;
    const std::uint32_t startTerrainZ = chunkIndexZ * terrain.chunkQuads;
    const std::uint32_t endTerrainX = std::min(startTerrainX + terrain.chunkQuads, terrainQuadsX);
    const std::uint32_t endTerrainZ = std::min(startTerrainZ + terrain.chunkQuads, terrainQuadsZ);
    const std::uint32_t maxWeightX = terrain.layerWeightWidth - 1U;
    const std::uint32_t maxWeightY = terrain.layerWeightHeight - 1U;
    const std::uint32_t minX = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(startTerrainX) * maxWeightX) / terrainQuadsX);
    const std::uint32_t minY = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(startTerrainZ) * maxWeightY) / terrainQuadsZ);
    const std::uint32_t maxX = std::min(
        maxWeightX,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(endTerrainX) * maxWeightX + terrainQuadsX - 1U) / terrainQuadsX));
    const std::uint32_t maxY = std::min(
        maxWeightY,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(endTerrainZ) * maxWeightY + terrainQuadsZ - 1U) / terrainQuadsZ));
    for (std::uint32_t y = minY; y <= maxY; ++y) {
        for (std::uint32_t x = minX; x <= maxX; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * terrain.layerWeightWidth + x) * 4U + layer;
            if (terrain.layerWeights[offset] != 0U) return true;
        }
    }
    return false;
}

[[nodiscard]] RenderStaticMeshVertexP3N3UV2 BuildVertex(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    const float cellX = terrain.worldSizeX / static_cast<float>(terrain.width - 1U);
    const float cellZ = terrain.worldSizeZ / static_cast<float>(terrain.height - 1U);
    const std::uint32_t left = x == 0U ? x : x - 1U;
    const std::uint32_t right = std::min(x + 1U, terrain.width - 1U);
    const std::uint32_t top = z == 0U ? z : z - 1U;
    const std::uint32_t bottom = std::min(z + 1U, terrain.height - 1U);
    const float dx = std::max(cellX * static_cast<float>(right - left), 0.000001F);
    const float dz = std::max(cellZ * static_cast<float>(bottom - top), 0.000001F);
    const float heightDx = terrain.heights[VertexIndex(terrain, right, z)] - terrain.heights[VertexIndex(terrain, left, z)];
    const float heightDz = terrain.heights[VertexIndex(terrain, x, bottom)] - terrain.heights[VertexIndex(terrain, x, top)];
    float nx = -heightDx / dx;
    float ny = 1.0F;
    float nz = -heightDz / dz;
    const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
    nx /= normalLength;
    ny /= normalLength;
    nz /= normalLength;
    const float u = static_cast<float>(x) / static_cast<float>(terrain.width - 1U);
    const float v = static_cast<float>(z) / static_cast<float>(terrain.height - 1U);
    return RenderStaticMeshVertexP3N3UV2{
        .x = u * terrain.worldSizeX - terrain.worldSizeX * 0.5F,
        .y = terrain.heights[VertexIndex(terrain, x, z)],
        .z = v * terrain.worldSizeZ - terrain.worldSizeZ * 0.5F,
        .nx = nx, .ny = ny, .nz = nz,
        .u = u * (terrain.worldSizeX / 4.0F),
        .v = v * (terrain.worldSizeZ / 4.0F),
        .u1 = u, .v1 = v,
    };
}

void BuildLodTable(RenderMeshAssetData& mesh) {
    mesh.lods.clear();
    std::size_t firstSection = 0U;
    while (firstSection < mesh.sections.size()) {
        const std::uint8_t level = mesh.sections[firstSection].lodLevel;
        std::size_t endSection = firstSection + 1U;
        while (endSection < mesh.sections.size() && mesh.sections[endSection].lodLevel == level) ++endSection;
        mesh.lods.push_back(RenderMeshLodDesc{
            .firstSection = static_cast<std::uint32_t>(firstSection),
            .sectionCount = static_cast<std::uint32_t>(endSection - firstSection),
            .firstMeshlet = static_cast<std::uint32_t>(firstSection),
            .meshletCount = static_cast<std::uint32_t>(endSection - firstSection),
            .minScreenCoverage = level == 0U ? 0.18F : (level == 1U ? 0.055F : (level == 2U ? 0.015F : 0.0F)),
        });
        firstSection = endSection;
    }
}

[[nodiscard]] std::uint64_t TerrainTopologyKey(const kb::assets::TerrainAsset& terrain) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::uint64_t value) noexcept {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    append(terrain.width);
    append(terrain.height);
    append(terrain.chunkQuads);
    append(terrain.lodCount);
    append(std::bit_cast<std::uint32_t>(terrain.worldSizeX));
    append(std::bit_cast<std::uint32_t>(terrain.worldSizeZ));
    append(terrain.materialLayers.size());
    append(terrain.layerWeightWidth);
    append(terrain.layerWeightHeight);
    for (const kb::assets::TerrainMaterialLayer& layer : terrain.materialLayers) {
        append(layer.materialAssetId);
    }
    for (const std::uint8_t hole : terrain.holes) append(hole);
    return hash == 0U ? 1U : hash;
}

void ExpandBounds(RenderBoundsSphere& bounds, const RenderStaticMeshVertexP3N3T4UV2& vertex) noexcept {
    const float dx = vertex.x - bounds.center[0];
    const float dy = vertex.y - bounds.center[1];
    const float dz = vertex.z - bounds.center[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!bounds.IsValid()) {
        bounds.center = { vertex.x, vertex.y, vertex.z };
        bounds.radius = 0.000001F;
    } else if (distance > bounds.radius) {
        const float expandedRadius = (bounds.radius + distance) * 0.5F;
        const float shift = (expandedRadius - bounds.radius) / distance;
        bounds.center[0] += dx * shift;
        bounds.center[1] += dy * shift;
        bounds.center[2] += dz * shift;
        bounds.radius = expandedRadius;
    }
}

[[nodiscard]] RenderStaticMeshVertexP3N3T4UV2 BuildTangentVertex(
    const kb::assets::TerrainAsset& terrain,
    std::uint32_t x,
    std::uint32_t z) noexcept {
    const RenderStaticMeshVertexP3N3UV2 source = BuildVertex(terrain, x, z);
    const float tangentLength = std::sqrt(source.ny * source.ny + source.nx * source.nx);
    const float tx = tangentLength > 0.000001F ? source.ny / tangentLength : 1.0F;
    const float ty = tangentLength > 0.000001F ? -source.nx / tangentLength : 0.0F;
    return RenderStaticMeshVertexP3N3T4UV2{
        .x = source.x, .y = source.y, .z = source.z,
        .nx = source.nx, .ny = source.ny, .nz = source.nz,
        .tx = tx, .ty = ty, .tz = 0.0F, .tw = 1.0F,
        .u = source.u, .v = source.v,
        .u1 = source.u1, .v1 = source.v1,
        .r = source.r, .g = source.g, .b = source.b, .a = source.a,
    };
}

} // namespace

std::optional<RenderMeshAssetData> RenderTerrainMeshBuilder::Build(const kb::assets::TerrainAsset& terrain) {
    if (!kb::assets::IsTerrainAssetValid(terrain)) return std::nullopt;

    RenderMeshAssetData mesh{};
    mesh.vertices.reserve(terrain.heights.size());
    for (std::uint32_t z = 0U; z < terrain.height; ++z) {
        for (std::uint32_t x = 0U; x < terrain.width; ++x) mesh.vertices.push_back(BuildVertex(terrain, x, z));
    }

    const std::uint32_t quadsX = terrain.width - 1U;
    const std::uint32_t quadsZ = terrain.height - 1U;
    const std::uint32_t maxLodsFromChunk = 1U + static_cast<std::uint32_t>(std::log2(static_cast<float>(terrain.chunkQuads)));
    const std::uint32_t lodCount = std::min(terrain.lodCount, maxLodsFromChunk);
    const std::uint32_t chunkCountX = (quadsX + terrain.chunkQuads - 1U) / terrain.chunkQuads;
    const std::uint32_t chunkCountZ = (quadsZ + terrain.chunkQuads - 1U) / terrain.chunkQuads;
    const std::uint32_t materialLayerCount = std::max<std::uint32_t>(
        1U, static_cast<std::uint32_t>(terrain.materialLayers.size()));
    std::vector<std::uint8_t> chunkLayerActive(
        static_cast<std::size_t>(chunkCountX) * chunkCountZ * materialLayerCount,
        1U);
    for (std::uint32_t chunkZ = 0U; chunkZ < chunkCountZ; ++chunkZ) {
        for (std::uint32_t chunkX = 0U; chunkX < chunkCountX; ++chunkX) {
            for (std::uint32_t layer = 1U; layer < materialLayerCount; ++layer) {
                const std::size_t activeIndex =
                    (static_cast<std::size_t>(chunkZ) * chunkCountX + chunkX) * materialLayerCount + layer;
                chunkLayerActive[activeIndex] = TerrainChunkLayerHasWeight(
                    terrain, chunkX, chunkZ, layer) ? 1U : 0U;
            }
        }
    }
    mesh.terrainChunkCountX = chunkCountX;
    mesh.terrainChunkCountZ = chunkCountZ;
    mesh.terrainLodCount = lodCount;
    mesh.terrainSectionIndices.assign(
        static_cast<std::size_t>(lodCount) * chunkCountX * chunkCountZ,
        UINT32_MAX);
    for (std::uint32_t lod = 0U; lod < lodCount; ++lod) {
        const std::uint32_t step = 1U << lod;
        for (std::uint32_t chunkZ = 0U; chunkZ < quadsZ; chunkZ += terrain.chunkQuads) {
            const std::uint32_t chunkEndZ = std::min(chunkZ + terrain.chunkQuads, quadsZ);
            for (std::uint32_t chunkX = 0U; chunkX < quadsX; chunkX += terrain.chunkQuads) {
                const std::uint32_t chunkEndX = std::min(chunkX + terrain.chunkQuads, quadsX);
                const std::uint32_t firstIndex = static_cast<std::uint32_t>(mesh.indices32.size());
                for (std::uint32_t z = chunkZ; z + step <= chunkEndZ; z += step) {
                    for (std::uint32_t x = chunkX; x + step <= chunkEndX; x += step) {
                        if (CoarseCellHasHole(terrain, x, z, step)) continue;
                        const std::uint32_t a = static_cast<std::uint32_t>(VertexIndex(terrain, x, z));
                        const std::uint32_t b = static_cast<std::uint32_t>(VertexIndex(terrain, x + step, z));
                        const std::uint32_t c = static_cast<std::uint32_t>(VertexIndex(terrain, x, z + step));
                        const std::uint32_t d = static_cast<std::uint32_t>(VertexIndex(terrain, x + step, z + step));
                        mesh.indices32.insert(mesh.indices32.end(), { a, c, b, b, c, d });
                    }
                }
                const std::uint32_t indexCount = static_cast<std::uint32_t>(mesh.indices32.size()) - firstIndex;
                if (indexCount != 0U) {
                    const std::size_t terrainSectionIndex =
                        static_cast<std::size_t>(lod) * chunkCountX * chunkCountZ +
                        static_cast<std::size_t>(chunkZ / terrain.chunkQuads) * chunkCountX +
                        chunkX / terrain.chunkQuads;
                    mesh.terrainSectionIndices[terrainSectionIndex] = static_cast<std::uint32_t>(mesh.sections.size());
                    for (std::uint32_t layer = 0U; layer < materialLayerCount; ++layer) {
                        const std::size_t activeIndex =
                            (static_cast<std::size_t>(chunkZ / terrain.chunkQuads) * chunkCountX +
                             chunkX / terrain.chunkQuads) * materialLayerCount + layer;
                        mesh.sections.push_back(RenderMeshSectionDesc{
                            .indexStart = firstIndex,
                            .indexCount = indexCount,
                            .materialSlot = layer,
                            .lodLevel = static_cast<std::uint8_t>(lod),
                            .terrainLayerIndex = terrain.materialLayers.empty()
                                ? UINT8_MAX
                                : static_cast<std::uint8_t>(layer),
                            .terrainLayerActive = chunkLayerActive[activeIndex] != 0U,
                        });
                    }
                }
            }
        }
    }
    if (mesh.indices32.empty() || !RenderMeshAssetBuilder::Finalize(
            mesh, RenderMeshFinalizeOptions{ .optimizeVertexFetch = false })) return std::nullopt;
    BuildLodTable(mesh);
    if (terrain.materialLayers.empty()) {
        mesh.materialSlots.push_back(RenderMaterialSlotDesc{});
        mesh.materialNames.emplace_back("Terrain");
        RenderMeshEmbeddedMaterial defaultMaterial{};
        defaultMaterial.name = "Terrain";
        defaultMaterial.desc.baseColor[0] = 1.0F;
        defaultMaterial.desc.baseColor[1] = 1.0F;
        defaultMaterial.desc.baseColor[2] = 1.0F;
        defaultMaterial.desc.baseColor[3] = 1.0F;
        defaultMaterial.desc.roughnessFactor = 0.92F;
        mesh.embeddedMaterials.push_back(std::move(defaultMaterial));
    } else {
        mesh.materialSlots.reserve(terrain.materialLayers.size());
        mesh.materialNames.reserve(terrain.materialLayers.size());
        for (std::uint32_t layer = 0U; layer < terrain.materialLayers.size(); ++layer) {
            mesh.materialSlots.push_back(RenderMaterialSlotDesc{
                .defaultMaterialAssetId = terrain.materialLayers[layer].materialAssetId,
            });
            mesh.materialNames.push_back("Terrain Layer " + std::to_string(layer + 1U));
        }
        mesh.terrainLayerWeights = terrain.layerWeights;
        mesh.terrainLayerWeightWidth = static_cast<std::uint16_t>(terrain.layerWeightWidth);
        mesh.terrainLayerWeightHeight = static_cast<std::uint16_t>(terrain.layerWeightHeight);
        mesh.terrainLayerCount = static_cast<std::uint8_t>(terrain.materialLayers.size());
    }
    mesh.dynamicTopologyKey = TerrainTopologyKey(terrain);
    mesh.RefreshDesc();
    return mesh;
}

bool RenderTerrainMeshBuilder::PrepareDynamicPreview(
    const kb::assets::TerrainAsset& terrain,
    RenderMeshAssetData& mesh) noexcept {
    const std::size_t vertexCount = static_cast<std::size_t>(terrain.width) * terrain.height;
    if (!kb::assets::IsTerrainAssetValid(terrain) ||
        mesh.tangentVertices.size() != vertexCount ||
        mesh.terrainChunkCountX == 0U || mesh.terrainChunkCountZ == 0U ||
        mesh.terrainLodCount == 0U ||
        mesh.terrainSectionIndices.size() != static_cast<std::size_t>(mesh.terrainChunkCountX) *
            mesh.terrainChunkCountZ * mesh.terrainLodCount ||
        mesh.dynamicTopologyKey != TerrainTopologyKey(terrain)) {
        return false;
    }
    mesh.dynamicVertexUpdates = true;
    mesh.vertexUpdateFirst = 0U;
    mesh.vertexUpdateCount = static_cast<std::uint32_t>(vertexCount);
    mesh.dynamicVertexUpdateRanges.clear();
    mesh.dynamicVertexUpdateRanges.push_back(RenderMeshVertexUpdateRange{
        .firstVertex = mesh.vertexUpdateFirst,
        .vertexCount = mesh.vertexUpdateCount,
    });
    mesh.dynamicSectionUpdateIndices.clear();
    mesh.dynamicSectionUpdateIndices.reserve(mesh.sections.size());
    for (std::uint32_t sectionIndex = 0U; sectionIndex < mesh.sections.size(); ++sectionIndex) {
        mesh.dynamicSectionUpdateIndices.push_back(sectionIndex);
    }
    mesh.dynamicTerrainLayerWeightUpdates.clear();
    if (mesh.terrainLayerCount != 0U) {
        mesh.dynamicTerrainLayerWeightUpdates.push_back(RenderTerrainLayerWeightUpdateRegion{
            .width = mesh.terrainLayerWeightWidth,
            .height = mesh.terrainLayerWeightHeight,
        });
    }
    mesh.RefreshDesc();
    return true;
}

bool RenderTerrainMeshBuilder::UpdateDynamicPreview(
    const kb::assets::TerrainAsset& terrain,
    const RenderTerrainMeshUpdateRegion& region,
    RenderMeshAssetData& mesh) noexcept {
    if (!mesh.dynamicVertexUpdates || mesh.tangentVertices.size() != terrain.heights.size() ||
        mesh.terrainChunkCountX == 0U || mesh.terrainChunkCountZ == 0U ||
        mesh.terrainLodCount == 0U ||
        region.minX > region.maxX || region.minZ > region.maxZ ||
        region.maxX >= terrain.width || region.maxZ >= terrain.height) {
        return false;
    }
    const std::uint32_t minX = region.minX == 0U ? 0U : region.minX - 1U;
    const std::uint32_t minZ = region.minZ == 0U ? 0U : region.minZ - 1U;
    const std::uint32_t maxX = std::min(region.maxX + 1U, terrain.width - 1U);
    const std::uint32_t maxZ = std::min(region.maxZ + 1U, terrain.height - 1U);
    float minimumHeight = std::numeric_limits<float>::max();
    float maximumHeight = std::numeric_limits<float>::lowest();
    for (std::uint32_t z = minZ; z <= maxZ; ++z) {
        for (std::uint32_t x = minX; x <= maxX; ++x) {
            RenderStaticMeshVertexP3N3T4UV2& vertex = mesh.tangentVertices[VertexIndex(terrain, x, z)];
            vertex = BuildTangentVertex(terrain, x, z);
            minimumHeight = std::min(minimumHeight, vertex.y);
            maximumHeight = std::max(maximumHeight, vertex.y);
        }
    }
    mesh.vertexUpdateFirst = minZ * terrain.width + minX;
    const std::uint32_t updateWidth = maxX - minX + 1U;
    mesh.vertexUpdateCount = updateWidth * (maxZ - minZ + 1U);
    for (std::uint32_t z = minZ; z <= maxZ; ++z) {
        mesh.dynamicVertexUpdateRanges.push_back(RenderMeshVertexUpdateRange{
            .firstVertex = z * terrain.width + minX,
            .vertexCount = updateWidth,
        });
    }

    const float worldMinX = static_cast<float>(minX) * (terrain.worldSizeX / static_cast<float>(terrain.width - 1U)) - terrain.worldSizeX * 0.5F;
    const float worldMaxX = static_cast<float>(maxX) * (terrain.worldSizeX / static_cast<float>(terrain.width - 1U)) - terrain.worldSizeX * 0.5F;
    const float worldMinZ = static_cast<float>(minZ) * (terrain.worldSizeZ / static_cast<float>(terrain.height - 1U)) - terrain.worldSizeZ * 0.5F;
    const float worldMaxZ = static_cast<float>(maxZ) * (terrain.worldSizeZ / static_cast<float>(terrain.height - 1U)) - terrain.worldSizeZ * 0.5F;
    const std::array<RenderStaticMeshVertexP3N3T4UV2, 8U> corners{
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = minimumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = minimumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = minimumHeight, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = minimumHeight, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = maximumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = maximumHeight, .z = worldMinZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMinX, .y = maximumHeight, .z = worldMaxZ },
        RenderStaticMeshVertexP3N3T4UV2{ .x = worldMaxX, .y = maximumHeight, .z = worldMaxZ },
    };
    for (const RenderStaticMeshVertexP3N3T4UV2& corner : corners) ExpandBounds(mesh.bounds, corner);
    const std::uint32_t minChunkX = minX == 0U ? 0U : (minX - 1U) / terrain.chunkQuads;
    const std::uint32_t minChunkZ = minZ == 0U ? 0U : (minZ - 1U) / terrain.chunkQuads;
    const std::uint32_t maxChunkX = std::min(maxX / terrain.chunkQuads, mesh.terrainChunkCountX - 1U);
    const std::uint32_t maxChunkZ = std::min(maxZ / terrain.chunkQuads, mesh.terrainChunkCountZ - 1U);
    for (std::uint32_t lod = 0U; lod < mesh.terrainLodCount; ++lod) {
        for (std::uint32_t chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            for (std::uint32_t chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                const std::size_t terrainSectionIndex =
                    static_cast<std::size_t>(lod) * mesh.terrainChunkCountX * mesh.terrainChunkCountZ +
                    static_cast<std::size_t>(chunkZ) * mesh.terrainChunkCountX + chunkX;
                if (terrainSectionIndex >= mesh.terrainSectionIndices.size()) continue;
                const std::uint32_t sectionIndex = mesh.terrainSectionIndices[terrainSectionIndex];
                if (sectionIndex == UINT32_MAX || sectionIndex >= mesh.sections.size()) continue;
                for (std::uint32_t layer = 0U; layer < std::max<std::uint32_t>(mesh.terrainLayerCount, 1U); ++layer) {
                    const std::uint32_t layeredSectionIndex = sectionIndex + layer;
                    if (layeredSectionIndex >= mesh.sections.size()) break;
                    RenderBoundsSphere& sectionBounds = mesh.sections[layeredSectionIndex].bounds;
                    for (const RenderStaticMeshVertexP3N3T4UV2& corner : corners) ExpandBounds(sectionBounds, corner);
                    if (layeredSectionIndex < mesh.meshlets.size()) mesh.meshlets[layeredSectionIndex].bounds = sectionBounds;
                    mesh.dynamicSectionUpdateIndices.push_back(layeredSectionIndex);
                }
            }
        }
    }
    mesh.RefreshDesc();
    return true;
}

bool RenderTerrainMeshBuilder::UpdateDynamicLayerPreview(
    const kb::assets::TerrainAsset& terrain,
    const RenderTerrainLayerWeightUpdateRegion& region,
    RenderMeshAssetData& mesh) noexcept {
    if (!mesh.dynamicVertexUpdates || terrain.materialLayers.empty() ||
        mesh.terrainLayerCount != terrain.materialLayers.size() ||
        mesh.terrainLayerWeightWidth != terrain.layerWeightWidth ||
        mesh.terrainLayerWeightHeight != terrain.layerWeightHeight ||
        mesh.terrainLayerWeights.size() != terrain.layerWeights.size() ||
        region.width == 0U || region.height == 0U ||
        static_cast<std::uint32_t>(region.x) + region.width > terrain.layerWeightWidth ||
        static_cast<std::uint32_t>(region.y) + region.height > terrain.layerWeightHeight) {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(region.width) * 4U;
    for (std::uint16_t row = 0U; row < region.height; ++row) {
        const std::size_t offset =
            (static_cast<std::size_t>(region.y + row) * terrain.layerWeightWidth + region.x) * 4U;
        std::copy_n(
            terrain.layerWeights.data() + offset,
            rowBytes,
            mesh.terrainLayerWeights.data() + offset);
    }
    const std::uint32_t terrainQuadsX = terrain.width - 1U;
    const std::uint32_t terrainQuadsZ = terrain.height - 1U;
    const std::uint32_t maxWeightX = terrain.layerWeightWidth - 1U;
    const std::uint32_t maxWeightY = terrain.layerWeightHeight - 1U;
    const std::uint32_t minTerrainX = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(region.x) * terrainQuadsX) / maxWeightX);
    const std::uint32_t minTerrainZ = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(region.y) * terrainQuadsZ) / maxWeightY);
    const std::uint32_t maxTerrainX = std::min(
        terrainQuadsX,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(region.x + region.width) * terrainQuadsX + maxWeightX - 1U) / maxWeightX));
    const std::uint32_t maxTerrainZ = std::min(
        terrainQuadsZ,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(region.y + region.height) * terrainQuadsZ + maxWeightY - 1U) / maxWeightY));
    const std::uint32_t minChunkX = std::min(minTerrainX / terrain.chunkQuads, mesh.terrainChunkCountX - 1U);
    const std::uint32_t minChunkZ = std::min(minTerrainZ / terrain.chunkQuads, mesh.terrainChunkCountZ - 1U);
    const std::uint32_t maxChunkX = std::min(maxTerrainX / terrain.chunkQuads, mesh.terrainChunkCountX - 1U);
    const std::uint32_t maxChunkZ = std::min(maxTerrainZ / terrain.chunkQuads, mesh.terrainChunkCountZ - 1U);
    for (std::uint32_t lod = 0U; lod < mesh.terrainLodCount; ++lod) {
        for (std::uint32_t chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            for (std::uint32_t chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
                const std::size_t terrainSectionIndex =
                    static_cast<std::size_t>(lod) * mesh.terrainChunkCountX * mesh.terrainChunkCountZ +
                    static_cast<std::size_t>(chunkZ) * mesh.terrainChunkCountX + chunkX;
                if (terrainSectionIndex >= mesh.terrainSectionIndices.size()) continue;
                const std::uint32_t firstSection = mesh.terrainSectionIndices[terrainSectionIndex];
                for (std::uint32_t layer = 1U; layer < mesh.terrainLayerCount; ++layer) {
                    const std::uint32_t sectionIndex = firstSection + layer;
                    if (sectionIndex >= mesh.sections.size()) continue;
                    const bool active = TerrainChunkLayerHasWeight(terrain, chunkX, chunkZ, layer);
                    if (mesh.sections[sectionIndex].terrainLayerActive != active) {
                        mesh.sections[sectionIndex].terrainLayerActive = active;
                        mesh.dynamicSectionUpdateIndices.push_back(sectionIndex);
                    }
                }
            }
        }
    }
    mesh.dynamicTerrainLayerWeightUpdates.push_back(region);
    mesh.RefreshDesc();
    return true;
}

} // namespace kb::render
