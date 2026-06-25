#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {
class AssetManager;
struct AssetMetadata;
}

namespace kb::render {

struct ResolvedRuntimeMaterialDesc {
    RenderMaterialDesc desc{};
    std::uint32_t unresolvedTexturePathCount = 0;
};

enum class RuntimeMaterialResolveStatus : std::uint8_t {
    Resolved,
    DefaultMaterial,
    ErrorMaterial,
};

enum class RuntimeMaterialResolveDiagnosticSeverity : std::uint8_t {
    Warning,
    Error,
};

enum class RuntimeMaterialResolveDiagnosticKind : std::uint8_t {
    MissingMaterialAsset,
    UnsupportedAssetType,
    MaterialLoadFailed,
    MaterialInstanceLoadFailed,
    MissingParentMaterial,
    ParentMaterialLoadFailed,
    UnresolvedTexturePath,
};

struct RuntimeMaterialResolveDiagnostic {
    RuntimeMaterialResolveDiagnosticSeverity severity = RuntimeMaterialResolveDiagnosticSeverity::Warning;
    RuntimeMaterialResolveDiagnosticKind kind = RuntimeMaterialResolveDiagnosticKind::MissingMaterialAsset;
    kb::assets::AssetId assetId{};
    kb::assets::AssetId parentAssetId{};
    std::filesystem::path path;
    std::string message;
};

struct ResolvedRuntimeMaterialAsset {
    ResolvedRuntimeMaterialDesc material{};
    std::vector<RuntimeMaterialResolveDiagnostic> diagnostics;
    std::uint64_t contentHash = 0;
    RuntimeMaterialResolveStatus status = RuntimeMaterialResolveStatus::Resolved;
    bool resolved = false;
};

class RuntimeMaterialResolver {
public:
    [[nodiscard]] static std::uint64_t EmbeddedMaterialAssetId(std::uint64_t meshAssetId, std::uint32_t slotIndex, std::string_view materialName) noexcept;
    [[nodiscard]] static RenderMaterialDesc DefaultMaterialDesc() noexcept;
    [[nodiscard]] static RenderMaterialDesc ErrorMaterialDesc() noexcept;

    [[nodiscard]] ResolvedRuntimeMaterialDesc ResolveEmbeddedMaterial(
        const kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& meshMetadata,
        const RenderMeshEmbeddedMaterial& embeddedMaterial) const;

    [[nodiscard]] ResolvedRuntimeMaterialDesc ResolveLoadedMaterial(
        const kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& materialMetadata,
        const RenderMaterialAssetData& materialAsset) const;

    [[nodiscard]] ResolvedRuntimeMaterialAsset ResolveAsset(
        kb::assets::AssetManager& manager,
        kb::assets::AssetId assetId) const;

    [[nodiscard]] ResolvedRuntimeMaterialAsset ResolveAsset(
        kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& metadata) const;

private:
    [[nodiscard]] std::uint64_t ResolveTextureAssetId(
        const kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& ownerMetadata,
        std::string_view texturePath) const;

    [[nodiscard]] std::uint64_t ResolveTextureAssetIdOrCount(
        const kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& ownerMetadata,
        std::string_view texturePath,
        std::uint32_t& unresolvedTexturePathCount) const;
};

} // namespace kb::render
