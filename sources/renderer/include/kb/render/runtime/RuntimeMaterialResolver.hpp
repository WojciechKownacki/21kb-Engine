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
    RenderMaterialGraphProgramBinding graphProgram{};
    std::vector<RenderMaterialGraphDiagnostic> graphDiagnostics;
};

enum class RuntimeMaterialResolveStatus : std::uint8_t {
    Resolved,
    DefaultMaterial,
    ErrorMaterial,
    LastGoodMaterial,
};

enum class RuntimeFallbackMaterialKind : std::uint8_t {
    Default,
    Error,
};

enum class RuntimeMaterialRenderMode : std::uint8_t {
    BuiltinPbr,
    CpuPbrFlatteningFallback,
    GpuMaterialGraph,
};

enum class RuntimeMaterialCpuFallbackReason : std::uint8_t {
    None,
    GraphProgramUnavailable,
};

[[nodiscard]] std::string_view RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode mode) noexcept;
[[nodiscard]] std::string_view RuntimeMaterialCpuFallbackReasonName(RuntimeMaterialCpuFallbackReason reason) noexcept;

struct RuntimeFallbackMaterialProfile {
    RuntimeFallbackMaterialKind kind = RuntimeFallbackMaterialKind::Default;
    RuntimeMaterialResolveStatus status = RuntimeMaterialResolveStatus::DefaultMaterial;
    std::string_view stableName;
    RenderMaterialDesc desc{};
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
    MaterialInstanceValidationFailed,
    MaterialTypeReferenceValidationFailed,
    MaterialGraphValidationFailed,
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
    RuntimeMaterialRenderMode renderMode = RuntimeMaterialRenderMode::BuiltinPbr;
    RuntimeMaterialCpuFallbackReason cpuFallbackReason = RuntimeMaterialCpuFallbackReason::None;
    RenderMaterialGraphArtifactFailurePolicy failurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    bool resolved = false;
};

class RuntimeMaterialResolver {
public:
    RuntimeMaterialResolver() = default;
    explicit RuntimeMaterialResolver(RenderMaterialGraphBuildContext graphBuildContext) noexcept;
    void SetGraphBuildContext(RenderMaterialGraphBuildContext graphBuildContext) noexcept;

    [[nodiscard]] static std::uint64_t EmbeddedMaterialAssetId(std::uint64_t meshAssetId, std::uint32_t slotIndex, std::string_view materialName) noexcept;
    [[nodiscard]] static std::uint64_t MaterialRuntimeContentHash(
        kb::assets::AssetManager& manager,
        const kb::assets::AssetMetadata& metadata);
    [[nodiscard]] static RuntimeFallbackMaterialProfile FallbackMaterialProfile(RuntimeFallbackMaterialKind kind) noexcept;
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

    // LIB-140: resolves a runtime MaterialInstance's parent material with `overrides` merged
    // onto its own baked graph parameter values (overrides win by stableId - see
    // MergeGraphParameterValues). Scope: `parentAssetId` must name a plain "RenderMaterial"
    // asset; a "RenderMaterialInstance" parent (instance-of-an-instance) is out of scope for
    // LIB-140 and resolves to the error material, same as an unsupported asset type.
    [[nodiscard]] ResolvedRuntimeMaterialAsset ResolveAssetWithParameterOverrides(
        kb::assets::AssetManager& manager,
        kb::assets::AssetId parentAssetId,
        const std::vector<RenderMaterialGraphParameterValue>& overrides) const;

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

    RenderMaterialGraphBuildContext graphBuildContext_{};
};

} // namespace kb::render
