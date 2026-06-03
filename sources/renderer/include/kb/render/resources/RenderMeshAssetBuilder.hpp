#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace kb::render {

struct RenderMeshAssetMaterialBinding {
    std::string materialName;
    std::uint64_t materialAssetId = 0;
};

struct RenderMeshObjImportDesc {
    const RenderMeshAssetMaterialBinding* materialBindings = nullptr;
    std::uint32_t materialBindingCount = 0;
    bool flipV = false;
};

struct RenderMeshGltfImportDesc {
    const RenderMeshAssetMaterialBinding* materialBindings = nullptr;
    std::uint32_t materialBindingCount = 0;
    bool flipV = false;
};

struct RenderMeshEmbeddedMaterial {
    std::string name;
    RenderMaterialDesc desc{};
    std::string albedoTexturePath;
    std::string normalTexturePath;
    std::string metallicRoughnessTexturePath;
    std::string occlusionTexturePath;
    std::string emissiveTexturePath;
    std::string clearcoatTexturePath;
    std::string clearcoatRoughnessTexturePath;
    std::string sheenColorTexturePath;
    std::string transmissionTexturePath;
    std::string thicknessTexturePath;
    std::string anisotropyTexturePath;
    std::string decalTexturePath;
    std::string layerMaskTexturePath;
};

struct RenderMeshAssetData {
    std::vector<RenderStaticMeshVertexP3N3UV2> vertices;
    std::vector<RenderStaticMeshVertexP3N3T4UV2> tangentVertices;
    std::vector<std::uint16_t> indices16;
    std::vector<std::uint32_t> indices32;
    std::vector<RenderMeshSectionDesc> sections;
    std::vector<RenderMeshletDesc> meshlets;
    std::vector<RenderMeshLodDesc> lods;
    std::vector<RenderMaterialSlotDesc> materialSlots;
    std::vector<std::string> materialNames;
    std::vector<RenderMeshEmbeddedMaterial> embeddedMaterials;
    RenderBoundsSphere bounds{};
    RenderMeshDesc desc{};

    RenderMeshDesc& RefreshDesc() noexcept;
};

class RenderMeshAssetBuilder {
public:
    RenderMeshAssetBuilder() = delete;

    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadObj(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadObj(std::istream& input, const RenderMeshObjImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadGltf(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc = {});
};

} // namespace kb::render
