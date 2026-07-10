#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <array>
#include <string>
#include <typeindex>
#include <vector>

namespace kb::assets {
class AssetManager;
struct AssetMetadata;
}

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialAssetDocumentVersion = 1U;
inline constexpr const char* kRenderMaterialAssetBuiltInPbrType = "builtin.pbr";
inline constexpr std::uint32_t kRenderMaterialAssetBuiltInPbrTypeVersion = 1U;

struct RenderMaterialGraphParameterValue {
    std::string stableId;
    RenderMaterialParameterType type = RenderMaterialParameterType::Scalar;
    std::array<float, 4U> numbers{};
    std::uint64_t assetId = 0U;
    bool boolValue = false;
    std::string text;
};

enum class RenderMaterialSchemaRefreshDiagnosticKind : std::uint8_t {
    AddedDefaultParameter,
    RemovedUnknownParameter,
    MaterialTypeChanged,
    ChangedParameterType,
};

struct RenderMaterialSchemaRefreshDiagnostic {
    RenderMaterialSchemaRefreshDiagnosticKind kind = RenderMaterialSchemaRefreshDiagnosticKind::AddedDefaultParameter;
    std::string stableId;
    std::string message;
};

/// RenderMaterialAssetData represents a Material asset (.kbmat file).
/// It stores the parent material defaults and shader metadata used by
/// material instances and direct material assignments.
struct RenderMaterialAssetData {
    std::uint32_t documentVersion = kRenderMaterialAssetDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    std::string materialType = kRenderMaterialAssetBuiltInPbrType;
    std::uint32_t materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    bool hasExplicitMaterialType = false;
    bool hasExplicitMaterialTypeVersion = false;
    std::uint64_t materialTypeAssetId = 0U;
    std::string materialTypeAssetPath;
    std::uint64_t graphSourceAssetId = 0U;
    std::string graphSourceAssetPath;
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
    std::vector<RenderMaterialGraphParameterValue> graphParameterValues;
    RenderMaterialGraphDocument graph{};
};

struct RenderMaterialSchemaRefreshResult {
    RenderMaterialAssetData material;
    std::vector<RenderMaterialSchemaRefreshDiagnostic> diagnostics;
};

enum class RenderMaterialTypeReferenceDiagnosticCode : std::uint8_t {
    MissingMaterialTypeReference,
    MissingMaterialTypeAsset,
    IncompatibleMaterialTypeAsset,
    MaterialTypeAssetLoadFailed,
    IncompatibleMaterialType,
    IncompatibleMaterialTypeVersion,
};

struct RenderMaterialTypeReferenceDiagnostic {
    RenderMaterialTypeReferenceDiagnosticCode code = RenderMaterialTypeReferenceDiagnosticCode::MissingMaterialTypeReference;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string message;
};

struct RenderMaterialTypeReferenceValidationResult {
    std::optional<RenderMaterialTypeDocument> materialType;
    std::vector<RenderMaterialTypeReferenceDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

enum class RenderMaterialAssetParseDiagnosticCode : std::uint8_t {
    FileOpenFailed,
    EmptyDocument,
    UnknownField,
    InvalidFieldValue,
    InvalidFloat,
    InvalidEnum,
    OutOfRange,
    UnsupportedAdvancedField,
    InvalidDocumentVersion,
    UnsupportedDocumentVersion,
    MissingMaterialType,
    UnsupportedMaterialType,
    InvalidMaterialTypeVersion,
    UnsupportedMaterialTypeVersion,
    TextureColorSpaceExpectation,
    InvalidGraphField,
    UnsupportedGraphVersion,
    GraphMigration,
    InvalidGraphNode,
    DuplicateGraphNode,
    InvalidGraphLink,
};

enum class RenderMaterialAssetParseDiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
};

struct RenderMaterialAssetParseDiagnostic {
    RenderMaterialAssetParseDiagnosticCode code = RenderMaterialAssetParseDiagnosticCode::InvalidFieldValue;
    RenderMaterialAssetParseDiagnosticSeverity severity = RenderMaterialAssetParseDiagnosticSeverity::Error;
    std::size_t line = 0U;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string field;
    std::string message;
    std::string text;
};

struct RenderMaterialAssetParseSourceContext {
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
};

[[nodiscard]] std::string_view RenderMaterialAssetParseDiagnosticCodeName(RenderMaterialAssetParseDiagnosticCode code) noexcept;
[[nodiscard]] std::string_view RenderMaterialTypeReferenceDiagnosticCodeName(RenderMaterialTypeReferenceDiagnosticCode code) noexcept;
[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialAssetGraphDiagnostics(const RenderMaterialAssetData& asset);
[[nodiscard]] RenderMaterialSchemaRefreshResult RefreshRenderMaterialGraphBackedMaterialSchema(
    const RenderMaterialAssetData& material,
    const RenderMaterialTypeDocument& materialType);
[[nodiscard]] RenderMaterialTypeReferenceValidationResult ValidateRenderMaterialTypeReference(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& materialMetadata,
    const kb::assets::AssetManager& manager);

struct RenderMaterialAssetParseResult {
    std::optional<RenderMaterialAssetData> asset;
    std::vector<RenderMaterialAssetParseDiagnostic> diagnostics;

    [[nodiscard]] bool HasErrors() const noexcept;
    [[nodiscard]] bool HasWarnings() const noexcept;
    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string DiagnosticMessage() const;
    [[nodiscard]] std::string ErrorMessage() const;
};

class RenderMaterialAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;

    [[nodiscard]] static std::optional<RenderMaterialAssetData> LoadMaterial(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialAssetData> LoadMaterial(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(std::istream& input);
    [[nodiscard]] static RenderMaterialAssetParseResult LoadMaterialWithDiagnostics(std::istream& input, const RenderMaterialAssetParseSourceContext& sourceContext);
    [[nodiscard]] static std::vector<kb::assets::AssetId> DiscoverMaterialDependencies(
        const RenderMaterialAssetData& material,
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry);
};

} // namespace kb::render
