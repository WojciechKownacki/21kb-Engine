#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/bake/AssetBakeKey.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <memory>
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
    // Buffer files an import carried with the document (ImportedAsset::resources); a buffer URI
    // they do not hold is read relative to the source path, when there is one.
    const kb::assets::ImportedAssetResource* externalResources = nullptr;
    std::uint32_t externalResourceCount = 0;
};

struct RenderMeshFbxImportDesc {
    const RenderMeshAssetMaterialBinding* materialBindings = nullptr;
    std::uint32_t materialBindingCount = 0;
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

// The streaming fragment a pack declares for one geometry chunk of a baked mesh.
struct RenderMeshChunkFragment {
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    std::uint32_t clusterCount = 0U;
};

// A baked mesh whose finer levels of detail stream. The loaded data's level 0 is the baked level
// `firstLod`; the primary block and the encoded chunks of the loaded levels are kept, so content
// streaming can rebuild the mesh with more levels as their chunks arrive
// (AssembleStreamedMeshLevels).
struct RenderMeshStreamingLayout {
    kb::assets::bake::AssetBakeDigest artifact{};
    std::uint32_t firstLod = 0U;
    // Per baked level.
    std::vector<std::uint32_t> lodFirstChunk;
    std::vector<std::uint32_t> lodChunkCount;
    std::vector<std::uint64_t> lodGeometryBytes;
    std::vector<float> lodErrors;
    std::shared_ptr<const std::vector<std::uint8_t>> primaryBlock;
    // Per chunk of the whole mesh: the fragment the pack declares for it.
    std::vector<RenderMeshChunkFragment> fragments;
    // The encoded chunks of levels firstLod..end, in order.
    std::vector<std::vector<std::uint8_t>> chunks;
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
    RenderBoundsBox boundsBox{};
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
    // Set for a packaged baked mesh whose finer levels of detail stream.
    std::optional<RenderMeshStreamingLayout> streaming;

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
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadGltf(
        std::span<const std::uint8_t> bytes,
        const std::filesystem::path& sourcePath,
        const RenderMeshGltfImportDesc& desc = {});
    [[nodiscard]] static std::optional<std::vector<std::filesystem::path>> GltfExternalBufferUris(
        std::span<const std::uint8_t> bytes);
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadFbx(const std::filesystem::path& path, const RenderMeshFbxImportDesc& desc = {});
    [[nodiscard]] static std::optional<RenderMeshAssetData> LoadFbx(std::span<const std::byte> data, const RenderMeshFbxImportDesc& desc = {});
    [[nodiscard]] static bool Finalize(
        RenderMeshAssetData& asset,
        const RenderMeshFinalizeOptions& options = {});
};

} // namespace kb::render
