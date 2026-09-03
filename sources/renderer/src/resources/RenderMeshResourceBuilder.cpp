#include "resources/RenderMeshResourceBuilder.hpp"

#include "resources/RenderMeshDescGeometry.hpp"
#include "resources/RenderMeshDescValidator.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] std::vector<RenderMeshSection> BuildSections(const RenderMeshDesc& desc, RenderBoundsSphere meshBounds) {
    if (desc.sectionCount == 0U) {
        return std::vector<RenderMeshSection>{
            RenderMeshSection{
                .indexStart = 0U,
                .indexCount = desc.indexCount,
                .vertexStart = 0U,
                .vertexCount = desc.vertexCount,
                .materialSlot = 0U,
                .bounds = meshBounds,
            },
        };
    }

    std::vector<RenderMeshSection> sections;
    sections.reserve(desc.sectionCount);
    for (std::uint32_t sectionIndex = 0U; sectionIndex < desc.sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = desc.sections[sectionIndex];
        sections.push_back(RenderMeshSection{
            .indexStart = section.indexStart,
            .indexCount = section.indexCount,
            .vertexStart = section.vertexStart,
            .vertexCount = RenderMeshDescGeometry::SectionVertexCount(desc, section),
            .materialSlot = section.materialSlot,
            .bounds = section.bounds.IsValid()
                ? section.bounds
                : RenderMeshDescGeometry::ComputeBounds(desc, section.indexStart, section.indexCount, section.vertexStart),
            .lodLevel = section.lodLevel,
            .terrainLayerIndex = section.terrainLayerIndex,
            .terrainLayerActive = section.terrainLayerActive,
        });
    }
    return sections;
}

[[nodiscard]] RenderBoundsSphere MergeBounds(RenderBoundsSphere lhs, RenderBoundsSphere rhs) noexcept {
    if (!lhs.IsValid()) return rhs;
    if (!rhs.IsValid()) return lhs;

    const float dx = rhs.center[0] - lhs.center[0];
    const float dy = rhs.center[1] - lhs.center[1];
    const float dz = rhs.center[2] - lhs.center[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (lhs.radius >= distance + rhs.radius) return lhs;
    if (rhs.radius >= distance + lhs.radius) return rhs;

    const float radius = (distance + lhs.radius + rhs.radius) * 0.5F;
    if (distance > 0.0F) {
        const float shift = (radius - lhs.radius) / distance;
        lhs.center[0] += dx * shift;
        lhs.center[1] += dy * shift;
        lhs.center[2] += dz * shift;
    }
    lhs.radius = radius;
    return lhs;
}

[[nodiscard]] RenderBoundsSphere ComputeMeshBounds(const RenderMeshDesc& desc) noexcept {
    if (desc.bounds.IsValid()) return desc.bounds;
    if (desc.sectionCount == 0U) {
        return RenderMeshDescGeometry::ComputeBounds(desc, 0U, desc.indexCount);
    }

    RenderBoundsSphere bounds{};
    for (std::uint32_t sectionIndex = 0U; sectionIndex < desc.sectionCount; ++sectionIndex) {
        const RenderMeshSectionDesc& section = desc.sections[sectionIndex];
        const RenderBoundsSphere sectionBounds = section.bounds.IsValid()
            ? section.bounds
            : RenderMeshDescGeometry::ComputeBounds(desc, section.indexStart, section.indexCount, section.vertexStart);
        bounds = MergeBounds(bounds, sectionBounds);
    }
    return bounds;
}

[[nodiscard]] std::vector<RenderMaterialSlot> BuildMaterialSlots(const RenderMeshDesc& desc, const std::vector<RenderMeshSection>& sections) {
    std::uint32_t requiredSlots = desc.materialSlotCount;
    for (const RenderMeshSection& section : sections) {
        requiredSlots = std::max(requiredSlots, section.materialSlot + 1U);
    }
    requiredSlots = std::max(requiredSlots, 1U);

    std::vector<RenderMaterialSlot> slots;
    slots.reserve(requiredSlots);
    for (std::uint32_t slotIndex = 0U; slotIndex < requiredSlots; ++slotIndex) {
        const std::uint64_t defaultMaterial = desc.materialSlots != nullptr && slotIndex < desc.materialSlotCount
            ? desc.materialSlots[slotIndex].defaultMaterialAssetId
            : 0U;
        slots.push_back(RenderMaterialSlot{
            .defaultMaterialAssetId = defaultMaterial,
        });
    }
    return slots;
}

[[nodiscard]] std::vector<RenderMeshletDesc> CopyMeshlets(const RenderMeshDesc& desc) {
    std::vector<RenderMeshletDesc> meshlets;
    if (desc.gpuDriven.meshletCount > 0U) {
        meshlets.assign(desc.gpuDriven.meshlets, desc.gpuDriven.meshlets + desc.gpuDriven.meshletCount);
    }
    return meshlets;
}

[[nodiscard]] std::vector<RenderMeshLodDesc> CopyLods(const RenderMeshDesc& desc) {
    std::vector<RenderMeshLodDesc> lods;
    if (desc.gpuDriven.lodCount > 0U) {
        lods.assign(desc.gpuDriven.lods, desc.gpuDriven.lods + desc.gpuDriven.lodCount);
    }
    return lods;
}

} // namespace

bool RenderMeshResourceBuilder::IsValidDesc(const RenderMeshDesc& desc) noexcept {
    return RenderMeshDescValidator::IsValid(desc);
}

const void* RenderMeshResourceBuilder::VertexData(const RenderMeshDesc& desc) noexcept {
    return RenderMeshDescGeometry::VertexData(desc);
}

RenderBoundsSphere RenderMeshResourceBuilder::ComputeBounds(
    const RenderMeshDesc& desc,
    std::uint32_t indexStart,
    std::uint32_t indexCount,
    std::uint32_t vertexStart) noexcept {
    return RenderMeshDescGeometry::ComputeBounds(desc, indexStart, indexCount, vertexStart);
}

RenderMeshResource RenderMeshResourceBuilder::Build(
    const RenderMeshDesc& desc,
    bgfx::VertexBufferHandle vertexBuffer,
    bgfx::DynamicVertexBufferHandle dynamicVertexBuffer,
    bgfx::IndexBufferHandle indexBuffer) {
    const RenderBoundsSphere meshBounds = ComputeMeshBounds(desc);
    std::vector<RenderMeshSection> sections = BuildSections(desc, meshBounds);
    std::vector<RenderMaterialSlot> materialSlots = BuildMaterialSlots(desc, sections);

    return RenderMeshResource{
        .vertexBuffer = vertexBuffer,
        .dynamicVertexBuffer = dynamicVertexBuffer,
        .indexBuffer = indexBuffer,
        .vertexCount = desc.vertexCount,
        .indexCount = desc.indexCount,
        .vertexFormat = desc.vertexFormat,
        .indexFormat = desc.indexFormat,
        .sections = std::move(sections),
        .materialSlots = std::move(materialSlots),
        .meshlets = CopyMeshlets(desc),
        .lods = CopyLods(desc),
        .bounds = meshBounds,
        .boundsBox = desc.boundsBox,
        .rasterStateExtra = desc.rasterStateExtra,
        .doubleSided = desc.doubleSided,
        .gpuCullingEnabled = desc.gpuDriven.allowGpuCulling && desc.gpuDriven.meshletCount > 0U,
        .indirectDrawsEnabled = desc.gpuDriven.allowIndirectDraws && desc.gpuDriven.meshletCount > 0U,
        .meshletCullingEnabled = desc.gpuDriven.allowMeshletCulling && desc.gpuDriven.meshletCount > 0U,
        .skinning = desc.skinning,
        .terrainLayerWeightWidth = desc.terrainLayerWeightWidth,
        .terrainLayerWeightHeight = desc.terrainLayerWeightHeight,
        .terrainLayerCount = desc.terrainLayerCount,
    };
}

} // namespace kb::render
