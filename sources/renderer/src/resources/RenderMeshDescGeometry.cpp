#include "resources/RenderMeshDescGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 3> VertexPosition(const RenderMeshDesc& desc, std::uint32_t vertexIndex) noexcept {
    const void* vertexData = RenderMeshDescGeometry::VertexData(desc);
    switch (desc.vertexFormat) {
    case RenderVertexFormat::P3C3: {
        const auto* vertices = static_cast<const RenderStaticMeshVertex*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::P3N3UV2: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexP3N3UV2*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::P3N3T4UV2: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexP3N3T4UV2*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4: {
        const auto* vertices = static_cast<const RenderStaticMeshVertexSkinned*>(vertexData);
        return { vertices[vertexIndex].x, vertices[vertexIndex].y, vertices[vertexIndex].z };
    }
    }

    return {};
}

} // namespace

const void* RenderMeshDescGeometry::VertexData(const RenderMeshDesc& desc) noexcept {
    return desc.vertexData != nullptr ? desc.vertexData : desc.vertices;
}

std::uint32_t RenderMeshDescGeometry::IndexAt(const RenderMeshDesc& desc, std::uint32_t index) noexcept {
    return desc.indexFormat == RenderIndexFormat::Uint32 ? desc.indices32[index] : desc.indices[index];
}

std::uint32_t RenderMeshDescGeometry::SectionVertexCount(
    const RenderMeshDesc& desc,
    const RenderMeshSectionDesc& section) noexcept {
    if (section.vertexStart >= desc.vertexCount) {
        return 0U;
    }
    return section.vertexCount == 0U ? desc.vertexCount - section.vertexStart : section.vertexCount;
}

RenderBoundsSphere RenderMeshDescGeometry::ComputeBounds(
    const RenderMeshDesc& desc,
    std::uint32_t indexStart,
    std::uint32_t indexCount,
    std::uint32_t vertexStart) noexcept {
    if (desc.vertexCount == 0U || vertexStart >= desc.vertexCount || indexCount == 0U ||
        indexStart >= desc.indexCount || indexCount > desc.indexCount - indexStart) {
        return {};
    }
    const std::uint32_t indexEnd = indexStart + indexCount;

    const std::uint32_t firstLocalIndex = IndexAt(desc, indexStart);
    if (firstLocalIndex >= desc.vertexCount - vertexStart) {
        return {};
    }
    const std::uint32_t firstVertexIndex = vertexStart + firstLocalIndex;

    const std::array<float, 3> first = VertexPosition(desc, firstVertexIndex);
    float minX = first[0];
    float minY = first[1];
    float minZ = first[2];
    float maxX = first[0];
    float maxY = first[1];
    float maxZ = first[2];
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t localIndex = IndexAt(desc, index);
        if (localIndex >= desc.vertexCount - vertexStart) {
            continue;
        }
        const std::uint32_t vertexIndex = vertexStart + localIndex;
        const std::array<float, 3> position = VertexPosition(desc, vertexIndex);
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
        const std::uint32_t localIndex = IndexAt(desc, index);
        if (localIndex >= desc.vertexCount - vertexStart) {
            continue;
        }
        const std::uint32_t vertexIndex = vertexStart + localIndex;
        const std::array<float, 3> position = VertexPosition(desc, vertexIndex);
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

} // namespace kb::render
