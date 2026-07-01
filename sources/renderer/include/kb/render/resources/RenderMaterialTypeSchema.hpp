#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kRenderMaterialTypeDocumentVersion = 1U;

enum class RenderMaterialParameterType : std::uint8_t {
    Scalar,
    Vec3,
    Vec4,
    Color,
    Enum,
    Bool,
    Texture,
};

enum class RenderMaterialParameterGroup : std::uint8_t {
    Core,
    Surface,
    Advanced,
};

enum class RenderMaterialTextureColorSpace : std::uint8_t {
    Srgb,
    Linear,
    Unknown,
};

enum class RenderMaterialFeatureSupport : std::uint8_t {
    Supported,
    ParsedButIgnored,
    NotApplicable,
};

enum class RenderMaterialTypeMigrationOperationKind : std::uint8_t {
    RenameParameter,
    SetDefault,
    RemoveUnsupported,
};

enum class RenderMaterialDomain : std::uint8_t {
    Surface,
    DeferredDecal,
    LightFunction,
    Volume,
    PostProcess,
    UserInterface,
};

// MAT-34: only Surface is implemented (drives the forward graph wrapper/pass). The other domains are
// declared for parity but NOT implemented; a graph that requests them falls back to Surface with a
// diagnostic instead of silently mis-rendering — zero false claim.
[[nodiscard]] constexpr bool IsRenderMaterialDomainProduction(RenderMaterialDomain domain) noexcept {
    return domain == RenderMaterialDomain::Surface;
}

[[nodiscard]] RenderMaterialDomain ParseRenderMaterialDomain(std::string_view text) noexcept;
[[nodiscard]] std::string_view RenderMaterialDomainName(RenderMaterialDomain domain) noexcept;

// MAT-37: surface shading models. Unlit (emissive/base color straight to the framebuffer) and DefaultLit
// (the metallic-roughness forward PBR path) are fully implemented and produce visibly different pixels.
// The remaining models are declared for parity but NOT implemented; a graph that requests one falls back
// to DefaultLit with a diagnostic instead of silently mis-shading — zero false claim.
enum class RenderMaterialShadingModel : std::uint8_t {
    Unlit,
    DefaultLit,
    Subsurface,
    ClearCoat,
    Cloth,
    Hair,
    Eye,
    SingleLayerWater,
    ThinTranslucent,
};

[[nodiscard]] constexpr bool IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel model) noexcept {
    return model == RenderMaterialShadingModel::Unlit || model == RenderMaterialShadingModel::DefaultLit;
}

[[nodiscard]] RenderMaterialShadingModel ParseRenderMaterialShadingModel(std::string_view text) noexcept;
[[nodiscard]] std::string_view RenderMaterialShadingModelName(RenderMaterialShadingModel model) noexcept;

// MAT-38: material blend modes (the UE-style flat enum). Opaque/Masked are opaque-pass; the four
// translucent modes resolve to real bgfx blend equations in MeshPipelinePassPolicy and to the
// BaseTransparent cook. Every mode is implemented (real GPU state), so all are production.
enum class RenderMaterialGraphBlendMode : std::uint8_t {
    Opaque,
    Masked,
    Translucent,
    Additive,
    Modulate,
    AlphaComposite,
    AlphaHoldout,
};

// True when the blend mode draws in the transparent pass with a blend equation (everything but Opaque/Masked).
[[nodiscard]] constexpr bool IsRenderMaterialGraphBlendModeTransparent(RenderMaterialGraphBlendMode mode) noexcept {
    return mode != RenderMaterialGraphBlendMode::Opaque && mode != RenderMaterialGraphBlendMode::Masked;
}

[[nodiscard]] RenderMaterialGraphBlendMode ParseRenderMaterialGraphBlendMode(std::string_view text) noexcept;
[[nodiscard]] std::string_view RenderMaterialGraphBlendModeName(RenderMaterialGraphBlendMode mode) noexcept;

enum class RenderMaterialShaderModel : std::uint8_t {
    MetallicRoughnessPbr,
};

enum class RenderMaterialBlendMode : std::uint8_t {
    Opaque,
    Masked,
    TransparentDisabled,
};

enum class RenderMaterialCullMode : std::uint8_t {
    BackFace,
    None,
};

struct RenderMaterialParameterRange {
    float min = 0.0F;
    float max = 1.0F;
};

struct RenderMaterialParameterSchema {
    std::string name;
    std::string displayName;
    RenderMaterialParameterType type = RenderMaterialParameterType::Scalar;
    RenderMaterialParameterGroup group = RenderMaterialParameterGroup::Core;
    RenderMaterialFeatureSupport runtimeSupport = RenderMaterialFeatureSupport::Supported;
    std::optional<RenderMaterialParameterRange> range;
    std::string defaultValueHint;
    std::string description;
    bool overrideSupported = true;
    std::uint32_t editorOrder = 0U;
};

struct RenderMaterialTextureSlotSchema {
    std::string name;
    std::string role;
    std::string assetIdFieldName;
    std::string pathFieldName;
    RenderMaterialTextureColorSpace expectedColorSpace = RenderMaterialTextureColorSpace::Unknown;
    RenderMaterialFeatureSupport runtimeSupport = RenderMaterialFeatureSupport::Supported;
    std::string description;
    std::string fallbackDescription;
    bool overrideSupported = true;
    std::uint32_t editorOrder = 0U;
};

struct RenderMaterialTypeMigrationOperation {
    RenderMaterialTypeMigrationOperationKind kind = RenderMaterialTypeMigrationOperationKind::RenameParameter;
    std::uint32_t fromVersion = 0U;
    std::uint32_t toVersion = 0U;
    std::string field;
    std::string targetField;
    std::string defaultValue;
    std::string reason;
};

struct RenderMaterialTypeRenderPass {
    std::string name;
    RenderMaterialFeatureSupport support = RenderMaterialFeatureSupport::Supported;
    std::string vertexShader;
    std::string fragmentShader;
};

struct RenderMaterialTypePermutationKey {
    std::string name;
    std::string defaultValue;
    std::vector<std::string> allowedValues;
};

struct RenderMaterialTypeRequiredResource {
    std::string name;
    std::string kind;
    bool required = true;
};

struct RenderMaterialTypeSchema {
    std::string typeName;
    std::uint32_t typeVersion = 0;
    std::vector<RenderMaterialParameterSchema> parameters;
    std::vector<RenderMaterialTextureSlotSchema> textureSlots;
    std::vector<std::string> alphaModes;
    std::vector<std::string> unsupportedAdvancedFeatures;
    std::vector<RenderMaterialTypeMigrationOperation> migrations;
};

struct RenderMaterialTypeDocument {
    std::uint32_t documentVersion = kRenderMaterialTypeDocumentVersion;
    std::string stableTypeId;
    std::uint32_t version = 0;
    std::string displayName;
    std::string description;
    RenderMaterialDomain domain = RenderMaterialDomain::Surface;
    RenderMaterialShaderModel shaderModel = RenderMaterialShaderModel::MetallicRoughnessPbr;
    RenderMaterialBlendMode defaultBlendMode = RenderMaterialBlendMode::Opaque;
    RenderMaterialCullMode defaultCullMode = RenderMaterialCullMode::BackFace;
    std::vector<RenderMaterialTypeRenderPass> renderPasses;
    std::vector<RenderMaterialTypePermutationKey> permutationKeys;
    std::vector<RenderMaterialTypeRequiredResource> requiredResources;
    RenderMaterialTypeSchema schema;
};

enum class RenderMaterialTypeDocumentDiagnosticCode : std::uint8_t {
    FileOpenFailed,
    InvalidDocumentVersion,
    UnsupportedDocumentVersion,
    MissingStableTypeId,
    InvalidTypeVersion,
    UnknownField,
    InvalidFieldValue,
};

struct RenderMaterialTypeDocumentDiagnostic {
    RenderMaterialTypeDocumentDiagnosticCode code = RenderMaterialTypeDocumentDiagnosticCode::InvalidDocumentVersion;
    std::size_t line = 0U;
    std::string field;
    std::string message;
    std::string text;
};

struct RenderMaterialTypeDocumentValidationResult {
    std::vector<RenderMaterialTypeDocumentDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct RenderMaterialTypeDocumentParseResult {
    std::optional<RenderMaterialTypeDocument> document;
    std::vector<RenderMaterialTypeDocumentDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept;
};

/// Returns the built-in PBR material type document (version 1).
[[nodiscard]] const RenderMaterialTypeDocument& GetBuiltInPbrMaterialTypeDocument() noexcept;
[[nodiscard]] std::string_view RenderMaterialTypeDocumentDiagnosticCodeName(RenderMaterialTypeDocumentDiagnosticCode code) noexcept;
[[nodiscard]] RenderMaterialTypeDocumentValidationResult ValidateRenderMaterialTypeDocument(const RenderMaterialTypeDocument& document);
[[nodiscard]] RenderMaterialTypeDocumentParseResult ParseRenderMaterialTypeDocument(std::istream& input);
void WriteRenderMaterialTypeDocument(std::ostream& output, const RenderMaterialTypeDocument& document);

/// Returns the built-in PBR material type schema (version 1).
[[nodiscard]] const RenderMaterialTypeSchema& GetBuiltInPbrMaterialTypeSchema() noexcept;

/// Finds a parameter schema by exact field name. Returns nullptr if not found.
[[nodiscard]] const RenderMaterialParameterSchema* FindMaterialParameterSchema(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept;

/// Finds a texture slot schema by its asset-id field name. Returns nullptr if not found.
[[nodiscard]] const RenderMaterialTextureSlotSchema* FindMaterialTextureSlotSchema(
    const RenderMaterialTypeSchema& schema, std::string_view assetIdFieldName) noexcept;

/// Checks if a field name corresponds to a texture path field in the schema.
[[nodiscard]] bool IsMaterialTexturePathField(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept;

[[nodiscard]] const RenderMaterialTypeMigrationOperation* FindMaterialTypeMigration(
    const RenderMaterialTypeSchema& schema,
    RenderMaterialTypeMigrationOperationKind kind,
    std::string_view field) noexcept;

} // namespace kb::render
