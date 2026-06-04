#include "resources/RenderMeshDescValidator.hpp"

#include "resources/RenderMeshDescGeometry.hpp"

namespace kb::render {
namespace {

[[nodiscard]] bool IsSupportedStaticMeshVertexFormat(RenderVertexFormat format) noexcept {
    switch (format) {
    case RenderVertexFormat::P3C3:
    case RenderVertexFormat::P3N3UV2:
    case RenderVertexFormat::P3N3T4UV2:
        return true;
    case RenderVertexFormat::SkinnedP3N3T4UV2J4W4:
        return false;
    }
    return false;
}

} // namespace

bool RenderMeshDescValidator::IsValid(const RenderMeshDesc& desc) noexcept {
    if (RenderMeshDescGeometry::VertexData(desc) == nullptr || desc.vertexCount == 0U || desc.indexCount == 0U) {
        return false;
    }
    if (!IsSupportedStaticMeshVertexFormat(desc.vertexFormat) || RenderStaticMeshVertexStride(desc.vertexFormat) == 0U) {
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
