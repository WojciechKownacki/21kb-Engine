#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include "resources/RenderMeshFbxImporter.hpp"
#include "resources/RenderMeshGltfImporter.hpp"
#include "resources/RenderMeshObjImporter.hpp"

namespace kb::render {

RenderMeshDesc& RenderMeshAssetData::RefreshDesc() noexcept {
    const bool hasTangents = !tangentVertices.empty();
    const void* vertexData = hasTangents
        ? (tangentVertices.empty() ? nullptr : static_cast<const void*>(tangentVertices.data()))
        : (vertices.empty() ? nullptr : static_cast<const void*>(vertices.data()));
    desc = RenderMeshDesc{
        .vertexData = vertexData,
        .vertexCount = static_cast<std::uint32_t>(hasTangents ? tangentVertices.size() : vertices.size()),
        .indices = indices16.empty() ? nullptr : indices16.data(),
        .indices32 = indices32.empty() ? nullptr : indices32.data(),
        .indexCount = static_cast<std::uint32_t>(indices16.empty() ? indices32.size() : indices16.size()),
        .vertexFormat = hasTangents ? RenderVertexFormat::P3N3T4UV2 : RenderVertexFormat::P3N3UV2,
        .indexFormat = indices16.empty() ? RenderIndexFormat::Uint32 : RenderIndexFormat::Uint16,
        .sections = sections.empty() ? nullptr : sections.data(),
        .sectionCount = static_cast<std::uint32_t>(sections.size()),
        .materialSlots = materialSlots.empty() ? nullptr : materialSlots.data(),
        .materialSlotCount = static_cast<std::uint32_t>(materialSlots.size()),
        .bounds = bounds,
        .gpuDriven = RenderGpuDrivenMeshDesc{
            .meshlets = meshlets.empty() ? nullptr : meshlets.data(),
            .meshletCount = static_cast<std::uint32_t>(meshlets.size()),
            .lods = lods.empty() ? nullptr : lods.data(),
            .lodCount = static_cast<std::uint32_t>(lods.size()),
        },
    };
    return desc;
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadObj(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc) {
    return RenderMeshObjImporter::Load(path, desc);
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadObj(std::istream& input, const RenderMeshObjImportDesc& desc) {
    return RenderMeshObjImporter::Load(input, desc);
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadGltf(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc) {
    return RenderMeshGltfImporter::Load(path, desc);
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadFbx(const std::filesystem::path& path, const RenderMeshFbxImportDesc& desc) {
    return RenderMeshFbxImporter::Load(path, desc);
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadFbx(std::span<const std::byte> data, const RenderMeshFbxImportDesc& desc) {
    return RenderMeshFbxImporter::Load(data, desc);
}

} // namespace kb::render
