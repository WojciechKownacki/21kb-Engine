#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialInstanceAssetDocumentVersion = 1U;

struct RenderMaterialInstanceStaticParameterOverride {
    std::string stableId;
    RenderMaterialGraphNodeKind nodeKind = RenderMaterialGraphNodeKind::StaticBoolParameter;
    std::string value;
};

struct RenderMaterialInstanceBasePropertyOverrides {
    bool overrideBlendMode = false;
    RenderMaterialGraphBlendMode blendMode = RenderMaterialGraphBlendMode::Opaque;
    bool overrideShadingModel = false;
    RenderMaterialShadingModel shadingModel = RenderMaterialShadingModel::DefaultLit;
    bool overrideTwoSided = false;
    bool twoSided = false;
    bool overrideOpacityMaskClip = false;
    float opacityMaskClip = 0.5F;
    bool overrideDomain = false;
    RenderMaterialDomain domain = RenderMaterialDomain::Surface;

    [[nodiscard]] bool HasAny() const noexcept {
        return overrideBlendMode || overrideShadingModel || overrideTwoSided || overrideOpacityMaskClip || overrideDomain;
    }
};

struct RenderMaterialInstanceAssetData {
    std::uint32_t documentVersion = kRenderMaterialInstanceAssetDocumentVersion;
    bool hasExplicitDocumentVersion = false;
    kb::assets::AssetId parentMaterialAssetId{};
    std::vector<RenderMaterialInstanceStaticParameterOverride> staticParameterOverrides;
    RenderMaterialInstanceBasePropertyOverrides basePropertyOverrides{};
    bool hasOverrides = false;
    RenderMaterialAssetData overrides{};
};

enum class RenderMaterialInstanceAssetParseDiagnosticCode : std::uint8_t {
    FileOpenFailed,
    EmptyDocument,
    UnknownField,
    InvalidFieldValue,
    InvalidDocumentVersion,
    UnsupportedDocumentVersion,
    MissingParentMaterial,
    InvalidOverrideMaterial,
};

struct RenderMaterialInstanceAssetParseDiagnostic {
    RenderMaterialInstanceAssetParseDiagnosticCode code = RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue;
    std::size_t line = 0U;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
    std::string field;
    std::string message;
    std::string text;
};

[[nodiscard]] std::string_view RenderMaterialInstanceAssetParseDiagnosticCodeName(RenderMaterialInstanceAssetParseDiagnosticCode code) noexcept;

struct RenderMaterialInstanceAssetParseResult {
    std::optional<RenderMaterialInstanceAssetData> asset;
    std::vector<RenderMaterialInstanceAssetParseDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ErrorMessage() const;
};

enum class RenderMaterialInstanceValidationDiagnosticCode : std::uint8_t {
    IncompatibleMaterialType,
    IncompatibleMaterialTypeVersion,
    UnknownOverrideParameter,
    IncompatibleOverrideParameterType,
    UnknownStaticOverrideParameter,
    IncompatibleStaticOverrideParameterType,
    InvalidStaticOverrideValue,
};

struct RenderMaterialInstanceValidationDiagnostic {
    RenderMaterialInstanceValidationDiagnosticCode code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType;
    std::string message;
};

struct RenderMaterialInstanceValidationResult {
    std::vector<RenderMaterialInstanceValidationDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

[[nodiscard]] std::string_view RenderMaterialInstanceValidationDiagnosticCodeName(RenderMaterialInstanceValidationDiagnosticCode code) noexcept;
[[nodiscard]] RenderMaterialAssetData BuildEffectiveRenderMaterialInstanceAsset(
    const RenderMaterialAssetData& parentMaterial,
    const RenderMaterialInstanceAssetData& instance);

// Upserts `materialValues` by stableId onto a copy of `parentValues` (override wins on a
// name collision, parent-only entries pass through unchanged). Shared by
// BuildEffectiveRenderMaterialInstanceAsset (authored .kbmatinstance overrides) and, since
// LIB-140, RuntimeMaterialResourceEnsurer's runtime MaterialInstance parameter-override
// resolution - both are "named override values merged onto a parent's baked values" in the
// same shape, so this is deliberate reuse rather than a new merge algorithm.
void MergeGraphParameterValues(
    std::vector<RenderMaterialGraphParameterValue>& materialValues,
    const std::vector<RenderMaterialGraphParameterValue>& parentValues);

class RenderMaterialInstanceAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;

    [[nodiscard]] static std::optional<RenderMaterialInstanceAssetData> LoadInstance(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<RenderMaterialInstanceAssetData> LoadInstance(std::istream& input);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(const std::filesystem::path& path);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId);
    [[nodiscard]] static RenderMaterialInstanceAssetParseResult LoadInstanceWithDiagnostics(std::istream& input);
    [[nodiscard]] static RenderMaterialInstanceValidationResult ValidateAgainstParent(
        const RenderMaterialInstanceAssetData& instance,
        const RenderMaterialAssetData& parentMaterial);
};

} // namespace kb::render
