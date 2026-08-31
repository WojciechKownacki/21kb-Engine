#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::render {

struct RenderMaterialCookPayload {
    std::string materialType = kRenderMaterialAssetBuiltInPbrType;
    std::uint32_t materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    kb::assets::AssetId materialTypeAssetId{};
    std::string materialTypeAssetPath;
    std::uint64_t sourceContentHash = 0U;
    std::uint64_t payloadHash = 0U;
    RenderMaterialDesc params{};
    std::vector<RenderMaterialGraphParameterValue> graphParameterValues;
    std::vector<kb::assets::AssetId> textureDependencies;
    bool graphBacked = false;
    bool graphCompileSucceeded = false;
    RenderMaterialGraphCompileArtifactCacheKey graphCompileKey{};
    RenderMaterialGraphShaderSource graphShader;
    std::vector<RenderMaterialGraphDiagnostic> graphDiagnostics;
};

struct RenderMaterialCookManifestEntry {
    kb::assets::AssetId materialAssetId{};
    std::string materialType = kRenderMaterialAssetBuiltInPbrType;
    std::uint32_t materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    kb::assets::AssetId materialTypeAssetId{};
    std::string materialTypeAssetPath;
    std::uint64_t sourceContentHash = 0U;
    std::uint64_t payloadHash = 0U;
    std::vector<kb::assets::AssetId> textureDependencies;
};

struct RenderMaterialCookManifest {
    std::vector<RenderMaterialCookManifestEntry> entries;
    std::uint64_t manifestHash = 0U;
};

struct RenderMaterialCookManifestInput {
    const RenderMaterialAssetData* material = nullptr;
    const kb::assets::AssetMetadata* metadata = nullptr;
};

class RenderMaterialCookPayloadBuilder final {
public:
    RenderMaterialCookPayloadBuilder() = delete;

    [[nodiscard]] static RenderMaterialCookPayload Build(
        const RenderMaterialAssetData& material,
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry);

    [[nodiscard]] static RenderMaterialCookPayload Build(
        const RenderMaterialAssetData& material,
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry,
        const RenderMaterialGraphFunctionLibrary& functionLibrary);
};

class RenderMaterialCookManifestBuilder final {
public:
    RenderMaterialCookManifestBuilder() = delete;

    [[nodiscard]] static RenderMaterialCookManifest Build(
        std::span<const RenderMaterialCookManifestInput> materials,
        const kb::assets::AssetRegistry& registry);
};

} // namespace kb::render
