#include "resources/RenderMeshAssetFinalizer.hpp"

#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace kb::render {
namespace {

void CompactIndices(RenderMeshAssetData& asset) {
    for (const std::uint32_t index : asset.indices32) {
        if (index > 0xFFFFU) {
            return;
        }
    }

    asset.indices16.clear();
    asset.indices16.reserve(asset.indices32.size());
    for (const std::uint32_t index : asset.indices32) {
        asset.indices16.push_back(static_cast<std::uint16_t>(index));
    }
    asset.indices32.clear();
}

[[nodiscard]] std::uint32_t IndexAt(const RenderMeshAssetData& asset, std::uint32_t index) noexcept {
    return asset.indices16.empty() ? asset.indices32[index] : asset.indices16[index];
}

[[nodiscard]] RenderBoundsSphere ComputeBounds(const RenderMeshAssetData& asset, std::uint32_t indexStart, std::uint32_t indexCount) noexcept {
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(asset.tangentVertices.empty() ? asset.vertices.size() : asset.tangentVertices.size());
    const std::uint32_t totalIndexCount = static_cast<std::uint32_t>(asset.indices16.empty() ? asset.indices32.size() : asset.indices16.size());
    if (vertexCount == 0U || indexCount == 0U || indexStart >= totalIndexCount || indexCount > totalIndexCount - indexStart) {
        return {};
    }
    const std::uint32_t indexEnd = indexStart + indexCount;

    const std::uint32_t firstVertexIndex = IndexAt(asset, indexStart);
    if (firstVertexIndex >= vertexCount) {
        return {};
    }

    auto vertexPosition = [&asset](std::uint32_t vertexIndex) noexcept {
        if (!asset.tangentVertices.empty()) {
            const RenderStaticMeshVertexP3N3T4UV2& vertex = asset.tangentVertices[vertexIndex];
            return std::array<float, 3>{ vertex.x, vertex.y, vertex.z };
        }
        const RenderStaticMeshVertexP3N3UV2& vertex = asset.vertices[vertexIndex];
        return std::array<float, 3>{ vertex.x, vertex.y, vertex.z };
    };

    const std::array<float, 3> first = vertexPosition(firstVertexIndex);
    float minX = first[0];
    float minY = first[1];
    float minZ = first[2];
    float maxX = first[0];
    float maxY = first[1];
    float maxZ = first[2];
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(asset, index);
        if (vertexIndex >= vertexCount) {
            continue;
        }
        const std::array<float, 3> position = vertexPosition(vertexIndex);
        minX = std::min(minX, position[0]);
        minY = std::min(minY, position[1]);
        minZ = std::min(minZ, position[2]);
        maxX = std::max(maxX, position[0]);
        maxY = std::max(maxY, position[1]);
        maxZ = std::max(maxZ, position[2]);
    }

    const std::array<float, 3> center{
        (minX + maxX) * 0.5F,
        (minY + maxY) * 0.5F,
        (minZ + maxZ) * 0.5F,
    };
    float radiusSquared = 0.0F;
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(asset, index);
        if (vertexIndex >= vertexCount) {
            continue;
        }
        const std::array<float, 3> position = vertexPosition(vertexIndex);
        const float dx = position[0] - center[0];
        const float dy = position[1] - center[1];
        const float dz = position[2] - center[2];
        radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
    }

    return RenderBoundsSphere{
        .center = center,
        .radius = std::sqrt(radiusSquared),
    };
}

void ComputeAssetBounds(RenderMeshAssetData& asset) noexcept {
    const std::uint32_t indexCount = static_cast<std::uint32_t>(asset.indices16.empty() ? asset.indices32.size() : asset.indices16.size());
    asset.bounds = ComputeBounds(asset, 0U, indexCount);
    for (RenderMeshSectionDesc& section : asset.sections) {
        if (!section.bounds.IsValid()) {
            section.bounds = ComputeBounds(asset, section.indexStart, section.indexCount);
        }
    }
}

void BuildGpuDrivenMetadata(RenderMeshAssetData& asset) {
    asset.meshlets.clear();
    asset.lods.clear();
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(asset.tangentVertices.empty() ? asset.vertices.size() : asset.tangentVertices.size());
    if (vertexCount == 0U || asset.sections.empty()) {
        return;
    }

    asset.meshlets.reserve(asset.sections.size());
    for (std::uint32_t sectionIndex = 0U; sectionIndex < asset.sections.size(); ++sectionIndex) {
        const RenderMeshSectionDesc& section = asset.sections[sectionIndex];
        asset.meshlets.push_back(RenderMeshletDesc{
            .indexStart = section.indexStart,
            .indexCount = section.indexCount,
            .vertexStart = 0U,
            .vertexCount = vertexCount,
            .sectionIndex = sectionIndex,
            .bounds = section.bounds.IsValid() ? section.bounds : asset.bounds,
            .cone = { 0.0F, 0.0F, 1.0F, 1.0F },
            .lodLevel = section.lodLevel,
        });
    }

    asset.lods.push_back(RenderMeshLodDesc{
        .firstSection = 0U,
        .sectionCount = static_cast<std::uint32_t>(asset.sections.size()),
        .firstMeshlet = 0U,
        .meshletCount = static_cast<std::uint32_t>(asset.meshlets.size()),
        .minScreenCoverage = 0.0F,
    });
}

[[nodiscard]] std::uint32_t MeshAssetVertexCount(const RenderMeshAssetData& asset) noexcept {
    return static_cast<std::uint32_t>(asset.tangentVertices.empty() ? asset.vertices.size() : asset.tangentVertices.size());
}

[[nodiscard]] bool ValidateMeshAssetIndices(const RenderMeshAssetData& asset) noexcept {
    const std::uint32_t vertexCount = MeshAssetVertexCount(asset);
    if (vertexCount == 0U || asset.indices32.empty()) {
        return false;
    }
    for (const std::uint32_t index : asset.indices32) {
        if (index >= vertexCount) {
            return false;
        }
    }
    for (const RenderMeshSectionDesc& section : asset.sections) {
        if (section.indexCount == 0U ||
            section.indexStart >= asset.indices32.size() ||
            section.indexCount > asset.indices32.size() - section.indexStart ||
            section.indexCount % 3U != 0U) {
            return false;
        }
    }
    return asset.indices32.size() % 3U == 0U;
}

void OptimizeMeshAssetVertexCache(RenderMeshAssetData& asset) {
    const std::uint32_t vertexCount = MeshAssetVertexCount(asset);
    if (vertexCount == 0U || asset.indices32.empty()) {
        return;
    }

    if (asset.sections.empty()) {
        std::vector<std::uint32_t> optimized(asset.indices32.size());
        meshopt_optimizeVertexCache(optimized.data(), asset.indices32.data(), asset.indices32.size(), vertexCount);
        asset.indices32 = std::move(optimized);
        return;
    }

    std::vector<std::uint32_t> optimizedSection;
    for (const RenderMeshSectionDesc& section : asset.sections) {
        optimizedSection.resize(section.indexCount);
        meshopt_optimizeVertexCache(
            optimizedSection.data(),
            asset.indices32.data() + section.indexStart,
            section.indexCount,
            vertexCount);
        std::copy(optimizedSection.begin(), optimizedSection.end(), asset.indices32.begin() + static_cast<std::ptrdiff_t>(section.indexStart));
    }
}

template <typename Vertex>
void OptimizeMeshAssetVertexFetch(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    std::vector<Vertex> optimized(vertices.size());
    const std::size_t optimizedVertexCount = meshopt_optimizeVertexFetch(
        optimized.data(),
        indices.data(),
        indices.size(),
        vertices.data(),
        vertices.size(),
        sizeof(Vertex));
    optimized.resize(optimizedVertexCount);
    vertices = std::move(optimized);
}

} // namespace

[[nodiscard]] std::array<float, 4> FallbackTangentForNormal(float nx, float ny, float nz) noexcept {
    const std::array<float, 3> axis = std::abs(ny) < 0.99F
        ? std::array<float, 3>{ 0.0F, 1.0F, 0.0F }
        : std::array<float, 3>{ 1.0F, 0.0F, 0.0F };
    std::array<float, 3> tangent{
        axis[1] * nz - axis[2] * ny,
        axis[2] * nx - axis[0] * nz,
        axis[0] * ny - axis[1] * nx,
    };
    const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    if (length > 0.0001F) {
        tangent[0] /= length;
        tangent[1] /= length;
        tangent[2] /= length;
    } else {
        tangent = { 1.0F, 0.0F, 0.0F };
    }
    return { tangent[0], tangent[1], tangent[2], 1.0F };
}

void RenderMeshAssetFinalizer::EnsureTangentVertexStorage(RenderMeshAssetData& asset) {
    if (!asset.tangentVertices.empty()) {
        return;
    }

    asset.tangentVertices.reserve(asset.vertices.size());
    for (const RenderStaticMeshVertexP3N3UV2& vertex : asset.vertices) {
        const std::array<float, 4> tangent = FallbackTangentForNormal(vertex.nx, vertex.ny, vertex.nz);
        asset.tangentVertices.push_back(RenderStaticMeshVertexP3N3T4UV2{
            .x = vertex.x,
            .y = vertex.y,
            .z = vertex.z,
            .nx = vertex.nx,
            .ny = vertex.ny,
            .nz = vertex.nz,
            .tx = tangent[0],
            .ty = tangent[1],
            .tz = tangent[2],
            .tw = tangent[3],
            .u = vertex.u,
            .v = vertex.v,
            .r = vertex.r,
            .g = vertex.g,
            .b = vertex.b,
        });
    }
    asset.vertices.clear();
}

bool RenderMeshAssetFinalizer::Finalize(RenderMeshAssetData& asset) {
    if (!ValidateMeshAssetIndices(asset)) {
        return false;
    }
    EnsureTangentVertexStorage(asset);
    OptimizeMeshAssetVertexCache(asset);
    if (!asset.tangentVertices.empty()) {
        OptimizeMeshAssetVertexFetch(asset.tangentVertices, asset.indices32);
    } else {
        OptimizeMeshAssetVertexFetch(asset.vertices, asset.indices32);
    }
    if (!ValidateMeshAssetIndices(asset)) {
        return false;
    }
    CompactIndices(asset);
    ComputeAssetBounds(asset);
    BuildGpuDrivenMetadata(asset);
    asset.RefreshDesc();
    return true;
}

} // namespace kb::render
