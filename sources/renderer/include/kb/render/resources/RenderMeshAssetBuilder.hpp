#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
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

struct RenderMeshFbxImportDesc {
    bool importMaterialSlots = true;
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

struct RenderMeshVertexUpdateRange {
    std::uint32_t firstVertex = 0U;
    std::uint32_t vertexCount = 0U;
};

struct RenderTerrainLayerWeightUpdateRegion {
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
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
    std::vector<std::uint32_t> terrainSectionIndices;
    RenderBoundsSphere bounds{};
    RenderMeshDesc desc{};
    std::uint64_t dynamicTopologyKey = 0U;
    std::vector<RenderMeshVertexUpdateRange> dynamicVertexUpdateRanges;
    std::vector<std::uint32_t> dynamicSectionUpdateIndices;
    std::vector<std::uint8_t> terrainLayerWeights;
    std::vector<RenderTerrainLayerWeightUpdateRegion> dynamicTerrainLayerWeightUpdates;
    std::uint32_t vertexUpdateFirst = 0U;
    std::uint32_t vertexUpdateCount = 0U;
    std::uint32_t terrainChunkCountX = 0U;
    std::uint32_t terrainChunkCountZ = 0U;
    std::uint32_t terrainLodCount = 0U;
    std::uint16_t terrainLayerWeightWidth = 0U;
    std::uint16_t terrainLayerWeightHeight = 0U;
    std::uint8_t terrainLayerCount = 0U;
    bool dynamicVertexUpdates = false;

    RenderMeshDesc& RefreshDesc() noexcept;
};

struct RenderMeshFinalizeOptions {
    bool optimizeVertexFetch = true;
};

class RenderMeshAssetBuilder {
public:
    RenderMeshAssetBuilder() = delete;

    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadObj(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadObj(std::istream& input, const RenderMeshObjImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadGltf(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadFbx(const std::filesystem::path& path, const RenderMeshFbxImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadFbx(std::span<const std::byte> data, const RenderMeshFbxImportDesc& desc = {});
    [[nodiscard]] static bool Finalize(
        RenderMeshAssetData& asset,
        const RenderMeshFinalizeOptions& options = {});
};

} // namespace kb::render
