#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

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
                    mesh.sections.push_back(RenderMeshSectionDesc{
                        .indexStart = firstIndex,
                        .indexCount = indexCount,
                        .materialSlot = 0U,
                        .lodLevel = static_cast<std::uint8_t>(lod),
                    });
                }
            }
        }
    }
    if (mesh.indices32.empty() || !RenderMeshAssetBuilder::Finalize(mesh)) return std::nullopt;
    BuildLodTable(mesh);
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
    mesh.RefreshDesc();
    return mesh;
}

} // namespace kb::render
