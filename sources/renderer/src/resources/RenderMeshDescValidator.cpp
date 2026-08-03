#include "resources/RenderMeshDescValidator.hpp"

#include "resources/RenderMeshDescGeometry.hpp"

#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] bool IsSupportedMeshVertexFormat(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3:
    case RenderVertexFormat::P3N3UV2:
    case RenderVertexFormat::P3N3T4UV2:
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsFinite(const RenderStaticMeshVertexSkinned& vertex) noexcept {
    const float values[]{
        vertex.x, vertex.y, vertex.z,
        vertex.nx, vertex.ny, vertex.nz,
        vertex.tx, vertex.ty, vertex.tz, vertex.tw,
        vertex.u, vertex.v,
        vertex.r, vertex.g, vertex.b,
        vertex.weights[0], vertex.weights[1], vertex.weights[2], vertex.weights[3],
    };
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

[[nodiscard]] bool HasValidSkinning(const RenderMeshDesc& desc) noexcept {
    if (desc.skinning.jointCount == 0U ||
        desc.skinning.jointCount > kRenderSkinnedVertexJointLimit) {
        return false;
    }
    const auto* vertices = static_cast<const RenderStaticMeshVertexSkinned*>(
        RenderMeshDescGeometry::VertexData(desc));
    for (std::uint32_t vertexIndex = 0U; vertexIndex < desc.vertexCount;
         ++vertexIndex) {
        const RenderStaticMeshVertexSkinned& vertex = vertices[vertexIndex];
        if (!IsFinite(vertex)) return false;
        float weightSum = 0.0F;
        for (std::size_t influence = 0U; influence < 4U; ++influence) {
            if (vertex.joints[influence] >= desc.skinning.jointCount ||
                vertex.weights[influence] < 0.0F) {
                return false;
            }
            weightSum += vertex.weights[influence];
        }
        if (!std::isfinite(weightSum) || weightSum <= 0.0F ||
            std::fabs(weightSum - 1.0F) > 0.0001F) {
            return false;
        }
    }
    return true;
}

} // namespace

bool RenderMeshDescValidator::IsValid(const RenderMeshDesc& desc) noexcept {
    if (RenderMeshDescGeometry::VertexData(desc) == nullptr || desc.vertexCount == 0U || desc.indexCount == 0U) {
        return false;
    }
    const std::uint32_t vertexStride = RenderStaticMeshVertexStride(desc.vertexFormat);
    if (!IsSupportedMeshVertexFormat(desc.vertexFormat) || vertexStride == 0U ||
        RenderStaticMeshVertexLayout(desc.vertexFormat).getStride() != vertexStride) {
        return false;
    }
    if (desc.vertexFormat == RenderVertexFormat::SkinnedP3N3T4UV2J4W4) {
        if (!HasValidSkinning(desc)) return false;
    } else if (desc.skinning.jointCount != 0U) {
        return false;
    }
    if (desc.indexFormat == RenderIndexFormat::Uint16 && desc.indices == nullptr) {
        return false;
    }
    if (desc.indexFormat == RenderIndexFormat::Uint32 && desc.indices32 == nullptr) {
        return false;
    }
    if (desc.sectionCount > 0U && desc.sections == nullptr) {
        return false;
    }
    for (std::uint32_t sectionIndex = 0; sectionIndex < desc.sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = desc.sections[sectionIndex];
        if (section.indexCount == 0U || section.indexStart >= desc.indexCount || section.indexCount > desc.indexCount - section.indexStart) {
            return false;
        }
    }
    if (desc.gpuDriven.meshletCount > 0U && desc.gpuDriven.meshlets == nullptr) {
        return false;
    }
    if (desc.gpuDriven.lodCount > 0U && desc.gpuDriven.lods == nullptr) {
        return false;
    }
    for (std::uint32_t meshletIndex = 0U; meshletIndex < desc.gpuDriven.meshletCount; ++meshletIndex) {
        const RenderMeshletDesc& meshlet = desc.gpuDriven.meshlets[meshletIndex];
        if (!meshlet.IsValid() ||
            meshlet.indexStart >= desc.indexCount ||
            meshlet.indexCount > desc.indexCount - meshlet.indexStart ||
            meshlet.vertexStart >= desc.vertexCount ||
            meshlet.vertexCount > desc.vertexCount - meshlet.vertexStart) {
            return false;
        }
    }
    for (std::uint32_t lodIndex = 0U; lodIndex < desc.gpuDriven.lodCount; ++lodIndex) {
        const RenderMeshLodDesc& lod = desc.gpuDriven.lods[lodIndex];
        if (!lod.IsValid() ||
            lod.firstSection > desc.sectionCount ||
            lod.sectionCount > desc.sectionCount - lod.firstSection ||
            lod.firstMeshlet > desc.gpuDriven.meshletCount ||
            lod.meshletCount > desc.gpuDriven.meshletCount - lod.firstMeshlet) {
            return false;
        }
    }
    for (std::uint32_t index = 0U; index < desc.indexCount; ++index) {
        if (RenderMeshDescGeometry::IndexAt(desc, index) >= desc.vertexCount) {
            return false;
        }
    }
    return true;
}

} // namespace kb::render
