#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshSourceImport.hpp"
#include "kb/render/resources/RenderTerrainMeshBuilder.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "resources/RenderMeshAssetFinalizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] std::string LowerExtensionText(std::string extension) {
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadObjPayload(const kb::assets::ImportedAsset& imported) {
    std::string text;
    text.resize(imported.payload.size());
    std::ranges::transform(imported.payload, text.begin(), [](std::byte value) {
        return static_cast<char>(value);
    });

    std::istringstream input{ text };
    return RenderMeshAssetBuilder::LoadObj(input);
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadGltfPayload(const kb::assets::AssetLoadRequest& request, const kb::assets::ImportedAsset& imported) {
    const auto payload = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(imported.payload.data()),
        imported.payload.size(),
    };
    std::optional<RenderMeshAssetData> mesh =
        RenderMeshAssetBuilder::LoadGltf(payload, {});
    if (mesh.has_value() &&
        (imported.importOptions & kb::assets::kAssetImportOptionMeshImportMaterials) != 0U) {
        std::vector<RenderMeshAssetMaterialBinding> bindings;
        bindings.reserve(mesh->materialNames.size());
        for (const std::string& materialName : mesh->materialNames) {
            const std::filesystem::path path = RenderMeshSourceImport::GeneratedMaterialVirtualPath(
                request.metadata.virtualPath, imported.sourceName, materialName);
            bindings.push_back({
                .materialName = materialName,
                .materialAssetId = kb::assets::MakeAssetId(
                    kb::assets::NormalizeAssetPath(path) + ":RenderMaterial").value,
            });
        }
        mesh = RenderMeshAssetBuilder::LoadGltf(payload, {}, RenderMeshGltfImportDesc{
            .materialBindings = bindings.data(),
            .materialBindingCount = static_cast<std::uint32_t>(bindings.size()),
        });
    }
    return mesh;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadImportedMesh(const kb::assets::AssetLoadRequest& request) {
    kb::assets::ImportedAssetLoader importedLoader;
    kb::assets::AssetLoadResult result = importedLoader.Load(request);
    if (!result.Succeeded()) {
        return std::nullopt;
    }

    const std::shared_ptr<kb::assets::ImportedAsset> imported = std::static_pointer_cast<kb::assets::ImportedAsset>(result.asset);
    if (imported == nullptr || imported->category != kb::assets::AssetImportCategory::Model) {
        return std::nullopt;
    }

    const std::string sourceExtension = LowerExtensionText(imported->sourceExtension);
    if (sourceExtension == ".obj") {
        return LoadObjPayload(*imported);
    }
    if (sourceExtension == ".gltf" || sourceExtension == ".glb") {
        return LoadGltfPayload(request, *imported);
    }
    if (sourceExtension == ".fbx") {
        const auto payload = std::span<const std::byte>{ imported->payload.data(), imported->payload.size() };
        std::optional<RenderMeshAssetData> mesh = RenderMeshAssetBuilder::LoadFbx(
            payload,
            RenderMeshFbxImportDesc{
                .importMaterialSlots = (imported->importOptions & kb::assets::kAssetImportOptionMeshDisableMaterialSlots) == 0U,
            });
        if (!mesh.has_value() ||
            (imported->importOptions & kb::assets::kAssetImportOptionMeshImportMaterials) == 0U) return mesh;
        std::vector<RenderMeshAssetMaterialBinding> bindings;
        bindings.reserve(mesh->materialNames.size());
        for (const std::string& materialName : mesh->materialNames) {
            const std::filesystem::path path = RenderMeshSourceImport::GeneratedMaterialVirtualPath(
                request.metadata.virtualPath, imported->sourceName, materialName);
            bindings.push_back({
                .materialName = materialName,
                .materialAssetId = kb::assets::MakeAssetId(
                    kb::assets::NormalizeAssetPath(path) + ":RenderMaterial").value,
            });
        }
        return RenderMeshAssetBuilder::LoadFbx(payload, RenderMeshFbxImportDesc{
            .materialBindings = bindings.data(),
            .materialBindingCount = static_cast<std::uint32_t>(bindings.size()),
            .importMaterialSlots = (imported->importOptions & kb::assets::kAssetImportOptionMeshDisableMaterialSlots) == 0U,
        });
    }
    return std::nullopt;
}

[[nodiscard]] std::uint32_t ReadLittleUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

struct MeshFragmentBounds {
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};

[[nodiscard]] MeshFragmentBounds BoundsOfVertexRange(
    const RenderMeshAssetData& asset,
    std::uint32_t firstVertex,
    std::uint32_t vertexCount) noexcept {
    const bool tangents = !asset.tangentVertices.empty();
    const std::uint32_t stride = static_cast<std::uint32_t>(
        tangents ? sizeof(RenderStaticMeshVertexP3N3T4UV2) : sizeof(RenderStaticMeshVertexP3N3UV2));
    const std::uint8_t* const vertices = tangents
        ? reinterpret_cast<const std::uint8_t*>(asset.tangentVertices.data())
        : reinterpret_cast<const std::uint8_t*>(asset.vertices.data());
    MeshFragmentBounds bounds{};
    for (std::uint32_t vertex = 0U; vertex < vertexCount; ++vertex) {
        std::array<float, 3> position{};
        std::memcpy(
            position.data(),
            vertices + static_cast<std::size_t>(firstVertex + vertex) * stride,
            sizeof(position));
        for (std::size_t axis = 0U; axis < position.size(); ++axis) {
            bounds.low[axis] = vertex == 0U ? position[axis] : std::min(bounds.low[axis], position[axis]);
            bounds.high[axis] = vertex == 0U ? position[axis] : std::max(bounds.high[axis], position[axis]);
        }
    }
    return bounds;
}

[[nodiscard]] bool ValidateMeshFragments(
    const kb::assets::bake::AssetPackReader& pack,
    const kb::assets::bake::AssetPackArtifactEntry& artifact,
    std::span<const std::vector<std::uint8_t>> chunks,
    const RenderMeshAssetData& asset) {
    const std::span<const kb::assets::bake::AssetPackFragmentEntry> fragments = pack.Fragments();
    if (artifact.blocks.size() != chunks.size() + 1U ||
        artifact.blocks.front().name != kb::assets::bake::kBakedAssetPrimaryBlockName ||
        artifact.blocks.front().residency != kb::assets::bake::BakedAssetBlockResidency::Resident ||
        fragments.size() != chunks.size()) {
        return false;
    }
    std::uint64_t firstVertex = 0U;
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const std::vector<std::uint8_t>& chunk = chunks[chunkIndex];
        if (chunk.size() < 24U) {
            return false;
        }
        const std::string blockName = kb::render::bake::BakedMeshChunkBlockName(chunkIndex);
        const kb::assets::bake::AssetPackBlockEntry& block = artifact.blocks[chunkIndex + 1U];
        const kb::assets::bake::AssetPackFragmentEntry& fragment = fragments[chunkIndex];
        if (block.name != blockName ||
            block.residency != kb::assets::bake::BakedAssetBlockResidency::Streaming ||
            block.storedBytes != chunk.size() || fragment.offset != block.offset ||
            fragment.bytes != block.storedBytes) {
            return false;
        }
        const std::uint32_t clusterCount = ReadLittleUInt32(chunk, 0U);
        const std::uint32_t vertexCount = ReadLittleUInt32(chunk, 4U);
        const std::uint64_t assetVertexCount = !asset.tangentVertices.empty()
            ? asset.tangentVertices.size()
            : asset.vertices.size();
        if (fragment.clusterCount != clusterCount ||
            firstVertex > assetVertexCount || vertexCount > assetVertexCount - firstVertex ||
            firstVertex > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const MeshFragmentBounds expected = BoundsOfVertexRange(
            asset, static_cast<std::uint32_t>(firstVertex), vertexCount);
        if (fragment.boundsMin != expected.low || fragment.boundsMax != expected.high) {
            return false;
        }
        firstVertex += vertexCount;
    }
    const std::uint64_t assetVertexCount = !asset.tangentVertices.empty()
        ? asset.tangentVertices.size()
        : asset.vertices.size();
    return firstVertex == assetVertexCount;
}

// Reads a baked mesh out of a package: the one artifact whose type this loader owns, its
// primary block, then every geometry chunk the chunk table names. A package carrying more than
// one mesh is refused rather than guessed at -- the same refusal the texture side already makes,
// and for the same reason: the artifact key says which bake it is, and nothing in the file says
// which of two bakes the caller wanted.
[[nodiscard]] std::optional<RenderMeshAssetData> LoadBakedMeshPackImpl(const std::filesystem::path& path) {
    kb::assets::bake::AssetPackReader pack;
    if (pack.Mount(path) != kb::assets::bake::AssetPackReadStatus::Success) {
        return std::nullopt;
    }
    if (pack.Artifacts().size() != 1U) {
        return std::nullopt;
    }
    const kb::assets::bake::AssetPackArtifactEntry* mesh = nullptr;
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : pack.Artifacts()) {
        if (artifact.assetTypeId != kb::render::bake::kMeshBakedAssetTypeId) {
            continue;
        }
        if (mesh != nullptr) {
            return std::nullopt;
        }
        mesh = &artifact;
    }
    if (mesh == nullptr) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> primaryBlock;
    if (pack.ReadBlock(*mesh, kb::assets::bake::kBakedAssetPrimaryBlockName, primaryBlock) !=
        kb::assets::bake::AssetPackReadStatus::Success) {
        return std::nullopt;
    }
    // Every block that is not the primary one is a chunk, and it is read in the order the chunk
    // table expects rather than the order the index happens to list them in.
    std::vector<std::vector<std::uint8_t>> chunks;
    for (std::uint32_t chunkIndex = 0U; chunkIndex + 1U < mesh->blocks.size(); ++chunkIndex) {
        std::vector<std::uint8_t> chunk;
        if (pack.ReadBlock(*mesh, kb::render::bake::BakedMeshChunkBlockName(chunkIndex), chunk) !=
            kb::assets::bake::AssetPackReadStatus::Success) {
            return std::nullopt;
        }
        chunks.push_back(std::move(chunk));
    }
    RenderMeshAssetData asset{};
    if (!kb::render::bake::ReadBakedMesh(primaryBlock, chunks, asset)) {
        return std::nullopt;
    }
    if (!ValidateMeshFragments(pack, *mesh, chunks, asset)) {
        return std::nullopt;
    }
    return asset;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadBakedMeshPack(const std::filesystem::path& path) {
    try {
        return LoadBakedMeshPackImpl(path);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    }
}

[[nodiscard]] bool ValidateRuntimeMeshFragments(
    const kb::assets::bake::RuntimeAssetPayload& payload,
    std::span<const std::vector<std::uint8_t>> chunks,
    const RenderMeshAssetData& asset) {
    if (payload.blocks.size() != chunks.size() + 1U || payload.blocks.empty() ||
        payload.blocks.front().name != kb::assets::bake::kBakedAssetPrimaryBlockName ||
        payload.blocks.front().residency != kb::assets::bake::BakedAssetBlockResidency::Resident) {
        return false;
    }
    std::uint64_t firstVertex = 0U;
    for (std::uint32_t chunkIndex = 0U; chunkIndex < chunks.size(); ++chunkIndex) {
        const std::vector<std::uint8_t>& chunk = chunks[chunkIndex];
        const kb::assets::bake::RuntimeAssetPayloadBlock& block = payload.blocks[chunkIndex + 1U];
        if (chunk.size() < 24U ||
            block.name != kb::render::bake::BakedMeshChunkBlockName(chunkIndex) ||
            block.residency != kb::assets::bake::BakedAssetBlockResidency::Streaming ||
            !block.fragment.has_value()) {
            return false;
        }
        const std::uint32_t clusterCount = ReadLittleUInt32(chunk, 0U);
        const std::uint32_t vertexCount = ReadLittleUInt32(chunk, 4U);
        const std::uint64_t assetVertexCount = !asset.tangentVertices.empty()
            ? asset.tangentVertices.size()
            : asset.vertices.size();
        if (block.fragment->clusterCount != clusterCount ||
            firstVertex > assetVertexCount || vertexCount > assetVertexCount - firstVertex ||
            firstVertex > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const MeshFragmentBounds expected = BoundsOfVertexRange(
            asset, static_cast<std::uint32_t>(firstVertex), vertexCount);
        if (block.fragment->boundsMin != expected.low || block.fragment->boundsMax != expected.high) {
            return false;
        }
        firstVertex += vertexCount;
    }
    const std::uint64_t assetVertexCount = !asset.tangentVertices.empty()
        ? asset.tangentVertices.size()
        : asset.vertices.size();
    return firstVertex == assetVertexCount;
}

[[nodiscard]] std::optional<RenderMeshAssetData> LoadBakedMeshPayload(
    const kb::assets::AssetLoadRequest& request,
    std::string& error) {
    kb::assets::bake::RuntimeAssetPayload payload{};
    if (!request.ReadPackagedPayload(
            kb::assets::bake::RuntimeArtifactEncoding::BakedMesh, {}, payload, error) ||
        payload.blocks.empty()) {
        return std::nullopt;
    }
    const auto primary = std::ranges::find(
        payload.blocks,
        kb::assets::bake::kBakedAssetPrimaryBlockName,
        &kb::assets::bake::RuntimeAssetPayloadBlock::name);
    if (primary == payload.blocks.end()) {
        error = "Baked mesh has no primary block";
        return std::nullopt;
    }
    std::vector<std::vector<std::uint8_t>> chunks;
    chunks.reserve(payload.blocks.size() - 1U);
    for (std::uint32_t chunkIndex = 0U; chunkIndex + 1U < payload.blocks.size(); ++chunkIndex) {
        const std::string name = kb::render::bake::BakedMeshChunkBlockName(chunkIndex);
        const auto block = std::ranges::find(
            payload.blocks, name, &kb::assets::bake::RuntimeAssetPayloadBlock::name);
        if (block == payload.blocks.end()) {
            error = "Baked mesh chunk sequence is incomplete";
            return std::nullopt;
        }
        chunks.push_back(block->bytes);
    }
    RenderMeshAssetData asset{};
    if (!kb::render::bake::ReadBakedMesh(primary->bytes, chunks, asset) ||
        !ValidateRuntimeMeshFragments(payload, chunks, asset)) {
        error = "Baked mesh payload is malformed";
        return std::nullopt;
    }
    return asset;
}

} // namespace

std::string_view RenderMeshAssetLoader::Type() const noexcept {
    return "RenderMesh";
}

std::type_index RenderMeshAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMeshAssetData);
}

std::vector<std::string> RenderMeshAssetLoader::Extensions() const {
    return { ".obj", ".gltf", ".glb", ".fbx", ".kbterrain",
        std::string{ kb::assets::bake::kAssetPackFileExtension } };
}

std::vector<std::string> RenderMeshAssetLoader::BakedAssetTypes() const {
    return { std::string{ kb::render::bake::kMeshBakedAssetTypeId } };
}

namespace {

[[nodiscard]] kb::assets::AssetLoadResult LoadRenderMeshAsset(const kb::assets::AssetLoadRequest& request) {
    std::optional<RenderMeshAssetData> mesh;
    std::string packagedError;
    const std::string extension = LowerExtensionText(request.SourceExtension());
    if (request.IsPackaged()) {
        mesh = LoadBakedMeshPayload(request, packagedError);
    } else if (extension == kb::assets::bake::kAssetPackFileExtension) {
        mesh = LoadBakedMeshPack(request.resolvedPath);
    } else if (extension == ".21kb") {
        mesh = LoadImportedMesh(request);
    } else if (request.HasSourceBytes()) {
        const std::span<const std::uint8_t> source = *request.sourceBytes;
        if (extension == ".kbterrain") {
            const std::optional<kb::assets::TerrainAsset> terrain =
                kb::assets::TerrainAssetIO::Load(source);
            if (terrain.has_value()) mesh = RenderTerrainMeshBuilder::Build(*terrain);
        } else if (extension == ".gltf" || extension == ".glb") {
            mesh = RenderMeshAssetBuilder::LoadGltf(source, request.resolvedPath);
        } else if (extension == ".fbx") {
            const auto bytes = std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(source.data()), source.size() };
            mesh = RenderMeshAssetBuilder::LoadFbx(bytes);
        } else {
            kb::assets::AssetMemoryInputStream input{ source };
            mesh = RenderMeshAssetBuilder::LoadObj(input);
        }
    } else {
        if (extension == ".kbterrain") {
            const std::optional<kb::assets::TerrainAsset> terrain = kb::assets::TerrainAssetIO::Load(request.resolvedPath);
            if (terrain.has_value()) mesh = RenderTerrainMeshBuilder::Build(*terrain);
        } else if (extension == ".gltf" || extension == ".glb") {
            mesh = RenderMeshAssetBuilder::LoadGltf(request.resolvedPath);
        } else if (extension == ".fbx") {
            mesh = RenderMeshAssetBuilder::LoadFbx(request.resolvedPath);
        } else {
            mesh = RenderMeshAssetBuilder::LoadObj(request.resolvedPath);
        }
    }
    if (!mesh.has_value()) {
        return kb::assets::AssetLoadResult{
            .error = packagedError.empty()
                ? "Render mesh import failed: " + request.resolvedPath.string()
                : std::move(packagedError),
        };
    }
    RenderMeshAssetFinalizer::EnsureTangentVertexStorage(*mesh);
    mesh->RefreshDesc();

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMeshAssetData>(std::move(*mesh)),
    };
}

} // namespace

kb::assets::AssetLoadResult RenderMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    try {
        return LoadRenderMeshAsset(request);
    } catch (const std::bad_alloc&) {
        return kb::assets::AssetLoadResult{ .error = "Render mesh load exceeded its memory budget" };
    } catch (const std::length_error&) {
        return kb::assets::AssetLoadResult{ .error = "Render mesh load requested an invalid allocation size" };
    }
}

} // namespace kb::render
