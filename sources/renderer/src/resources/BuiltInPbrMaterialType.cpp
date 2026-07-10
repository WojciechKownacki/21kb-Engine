#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <array>
#include <charconv>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseUInt32(std::string_view text, std::uint32_t& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& output) noexcept {
    if (text == "true" || text == "1") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0") {
        output = false;
        return true;
    }
    return false;
}

void AddDiagnostic(
    std::vector<RenderMaterialTypeDocumentDiagnostic>& diagnostics,
    RenderMaterialTypeDocumentDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text = {}) {
    diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] std::string_view ParameterTypeName(RenderMaterialParameterType type) noexcept {
    switch (type) {
    case RenderMaterialParameterType::Scalar: return "Scalar";
    case RenderMaterialParameterType::Vec3: return "Vec3";
    case RenderMaterialParameterType::Vec4: return "Vec4";
    case RenderMaterialParameterType::Color: return "Color";
    case RenderMaterialParameterType::Enum: return "Enum";
    case RenderMaterialParameterType::Bool: return "Bool";
    case RenderMaterialParameterType::Texture: return "Texture";
    }
    return "Scalar";
}

[[nodiscard]] bool ParseParameterType(std::string_view text, RenderMaterialParameterType& output) noexcept {
    if (text == "Scalar") output = RenderMaterialParameterType::Scalar;
    else if (text == "Vec3") output = RenderMaterialParameterType::Vec3;
    else if (text == "Vec4") output = RenderMaterialParameterType::Vec4;
    else if (text == "Color") output = RenderMaterialParameterType::Color;
    else if (text == "Enum") output = RenderMaterialParameterType::Enum;
    else if (text == "Bool") output = RenderMaterialParameterType::Bool;
    else if (text == "Texture") output = RenderMaterialParameterType::Texture;
    else return false;
    return true;
}

[[nodiscard]] std::string_view ParameterGroupName(RenderMaterialParameterGroup group) noexcept {
    switch (group) {
    case RenderMaterialParameterGroup::Core: return "Core";
    case RenderMaterialParameterGroup::Surface: return "Surface";
    case RenderMaterialParameterGroup::Advanced: return "Advanced";
    }
    return "Core";
}

[[nodiscard]] bool ParseParameterGroup(std::string_view text, RenderMaterialParameterGroup& output) noexcept {
    if (text == "Core") output = RenderMaterialParameterGroup::Core;
    else if (text == "Surface") output = RenderMaterialParameterGroup::Surface;
    else if (text == "Advanced") output = RenderMaterialParameterGroup::Advanced;
    else return false;
    return true;
}

[[nodiscard]] std::string_view FeatureSupportName(RenderMaterialFeatureSupport support) noexcept {
    switch (support) {
    case RenderMaterialFeatureSupport::Supported: return "Supported";
    case RenderMaterialFeatureSupport::ParsedButIgnored: return "ParsedButIgnored";
    case RenderMaterialFeatureSupport::NotApplicable: return "NotApplicable";
    }
    return "Supported";
}

[[nodiscard]] bool ParseFeatureSupport(std::string_view text, RenderMaterialFeatureSupport& output) noexcept {
    if (text == "Supported") output = RenderMaterialFeatureSupport::Supported;
    else if (text == "ParsedButIgnored") output = RenderMaterialFeatureSupport::ParsedButIgnored;
    else if (text == "NotApplicable") output = RenderMaterialFeatureSupport::NotApplicable;
    else return false;
    return true;
}

[[nodiscard]] std::string_view TextureColorSpaceName(RenderMaterialTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderMaterialTextureColorSpace::Srgb: return "Srgb";
    case RenderMaterialTextureColorSpace::Linear: return "Linear";
    case RenderMaterialTextureColorSpace::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] bool ParseTextureColorSpace(std::string_view text, RenderMaterialTextureColorSpace& output) noexcept {
    if (text == "Srgb" || text == "sRGB") output = RenderMaterialTextureColorSpace::Srgb;
    else if (text == "Linear") output = RenderMaterialTextureColorSpace::Linear;
    else if (text == "Unknown") output = RenderMaterialTextureColorSpace::Unknown;
    else return false;
    return true;
}

[[nodiscard]] std::string_view DomainName(RenderMaterialDomain domain) noexcept {
    switch (domain) {
    case RenderMaterialDomain::Surface: return "Surface";
    }
    return "Surface";
}

[[nodiscard]] bool ParseDomain(std::string_view text, RenderMaterialDomain& output) noexcept {
    if (text != "Surface") {
        return false;
    }
    output = RenderMaterialDomain::Surface;
    return true;
}

[[nodiscard]] std::string_view ShaderModelName(RenderMaterialShaderModel shaderModel) noexcept {
    switch (shaderModel) {
    case RenderMaterialShaderModel::MetallicRoughnessPbr: return "MetallicRoughnessPbr";
    }
    return "MetallicRoughnessPbr";
}

[[nodiscard]] bool ParseShaderModel(std::string_view text, RenderMaterialShaderModel& output) noexcept {
    if (text != "MetallicRoughnessPbr") {
        return false;
    }
    output = RenderMaterialShaderModel::MetallicRoughnessPbr;
    return true;
}

[[nodiscard]] std::string_view BlendModeName(RenderMaterialBlendMode blendMode) noexcept {
    switch (blendMode) {
    case RenderMaterialBlendMode::Opaque: return "Opaque";
    case RenderMaterialBlendMode::Masked: return "Masked";
    case RenderMaterialBlendMode::TransparentDisabled: return "TransparentDisabled";
    }
    return "Opaque";
}

[[nodiscard]] bool ParseBlendMode(std::string_view text, RenderMaterialBlendMode& output) noexcept {
    if (text == "Opaque") output = RenderMaterialBlendMode::Opaque;
    else if (text == "Masked") output = RenderMaterialBlendMode::Masked;
    else if (text == "TransparentDisabled") output = RenderMaterialBlendMode::TransparentDisabled;
    else return false;
    return true;
}

[[nodiscard]] std::string_view CullModeName(RenderMaterialCullMode cullMode) noexcept {
    switch (cullMode) {
    case RenderMaterialCullMode::BackFace: return "BackFace";
    case RenderMaterialCullMode::None: return "None";
    }
    return "BackFace";
}

[[nodiscard]] bool ParseCullMode(std::string_view text, RenderMaterialCullMode& output) noexcept {
    if (text == "BackFace") output = RenderMaterialCullMode::BackFace;
    else if (text == "None") output = RenderMaterialCullMode::None;
    else return false;
    return true;
}

[[nodiscard]] std::string_view MigrationOperationKindName(RenderMaterialTypeMigrationOperationKind kind) noexcept {
    switch (kind) {
    case RenderMaterialTypeMigrationOperationKind::RenameParameter: return "RenameParameter";
    case RenderMaterialTypeMigrationOperationKind::SetDefault: return "SetDefault";
    case RenderMaterialTypeMigrationOperationKind::RemoveUnsupported: return "RemoveUnsupported";
    }
    return "RenameParameter";
}

[[nodiscard]] bool ParseMigrationOperationKind(std::string_view text, RenderMaterialTypeMigrationOperationKind& output) noexcept {
    if (text == "RenameParameter") output = RenderMaterialTypeMigrationOperationKind::RenameParameter;
    else if (text == "SetDefault") output = RenderMaterialTypeMigrationOperationKind::SetDefault;
    else if (text == "RemoveUnsupported") output = RenderMaterialTypeMigrationOperationKind::RemoveUnsupported;
    else return false;
    return true;
}

[[nodiscard]] std::string JoinAllowedValues(const std::vector<std::string>& values) {
    std::string joined;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index > 0U) {
            joined += '|';
        }
        joined += values[index];
    }
    return joined;
}

[[nodiscard]] std::vector<std::string> SplitAllowedValues(std::string_view text) {
    std::vector<std::string> values;
    while (!text.empty()) {
        const std::size_t split = text.find('|');
        const std::string_view value = split == std::string_view::npos ? text : text.substr(0U, split);
        if (!value.empty()) {
            values.emplace_back(value);
        }
        if (split == std::string_view::npos) {
            break;
        }
        text.remove_prefix(split + 1U);
    }
    return values;
}

[[nodiscard]] std::string EncodeToken(std::string_view value) {
    std::string encoded;
    for (const char ch : value) {
        switch (ch) {
        case '%':
            encoded += "%25";
            break;
        case ' ':
            encoded += "%20";
            break;
        case '\t':
            encoded += "%09";
            break;
        case '#':
            encoded += "%23";
            break;
        default:
            encoded += ch;
            break;
        }
    }
    return encoded;
}

[[nodiscard]] std::string DecodeToken(std::string_view value) {
    std::string decoded;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2U < value.size()) {
            const std::string_view code = value.substr(index + 1U, 2U);
            if (code == "20") {
                decoded += ' ';
                index += 2U;
                continue;
            }
            if (code == "09") {
                decoded += '\t';
                index += 2U;
                continue;
            }
            if (code == "25") {
                decoded += '%';
                index += 2U;
                continue;
            }
            if (code == "23") {
                decoded += '#';
                index += 2U;
                continue;
            }
        }
        decoded += value[index];
    }
    return decoded;
}

const RenderMaterialTypeSchema& BuildBuiltInPbrMaterialTypeSchema() {
    static const std::array<RenderMaterialParameterSchema, 27> parameters{{
        // Core PBR parameters (MVP)
        { .name = "baseColor", .type = RenderMaterialParameterType::Vec4, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1 1 1 1", .description = "Base color and opacity (RGBA)." },
        { .name = "baseColorFactor", .type = RenderMaterialParameterType::Vec4, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1 1 1 1", .description = "Alias for baseColor." },
        { .name = "emissiveColor", .type = RenderMaterialParameterType::Vec3, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0 0 0", .description = "Emissive color (RGB)." },
        { .name = "emissiveFactor", .type = RenderMaterialParameterType::Vec3, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0 0 0", .description = "Alias for emissiveColor." },
        { .name = "metallicFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Metallic factor (0 = dielectric, 1 = metal)." },
        { .name = "roughnessFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1", .description = "Perceptual roughness (0 = smooth, 1 = rough)." },
        { .name = "normalScale", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 8.0F }, .defaultValueHint = "1", .description = "Normal map scale multiplier." },
        { .name = "occlusionStrength", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1", .description = "Ambient occlusion strength." },
        { .name = "emissiveStrength", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 64.0F }, .defaultValueHint = "1", .description = "Emissive strength multiplier." },
        { .name = "alphaCutoff", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0.5", .description = "Alpha test threshold for MASK mode." },
        { .name = "alphaMode", .type = RenderMaterialParameterType::Enum, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .defaultValueHint = "OPAQUE", .description = "Alpha mode: OPAQUE and MASK render in the opaque pass; BLEND renders alpha-blended in the transparent pass." },
        { .name = "doubleSided", .type = RenderMaterialParameterType::Bool, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .defaultValueHint = "false", .description = "Render both front and back faces." },

        // Surface / advanced PBR parameters (parsed but ignored by current runtime shader)
        { .name = "clearcoatFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Clearcoat layer intensity." },
        { .name = "clearcoatRoughnessFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Clearcoat roughness." },
        { .name = "sheenColor", .type = RenderMaterialParameterType::Vec3, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0 0 0", .description = "Sheen color (RGB)." },
        { .name = "sheenRoughnessFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Sheen roughness." },
        { .name = "transmissionFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Light transmission factor." },
        { .name = "thicknessFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1000.0F }, .defaultValueHint = "0", .description = "Volume thickness." },
        { .name = "attenuationColor", .type = RenderMaterialParameterType::Vec3, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1 1 1", .description = "Volume attenuation color." },
        { .name = "attenuationDistance", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1000000.0F }, .defaultValueHint = "0", .description = "Volume attenuation distance." },
        { .name = "subsurfaceColor", .type = RenderMaterialParameterType::Vec3, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1 1 1", .description = "Subsurface scattering color." },
        { .name = "subsurfaceFactor", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Subsurface scattering factor." },
        { .name = "anisotropyStrength", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "0", .description = "Anisotropic specular strength." },
        { .name = "anisotropyRotation", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ -1.0F, 1.0F }, .defaultValueHint = "0", .description = "Anisotropic rotation." },
        { .name = "layerWeight", .type = RenderMaterialParameterType::Scalar, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .range = RenderMaterialParameterRange{ 0.0F, 1.0F }, .defaultValueHint = "1", .description = "Layer blend weight." },
        { .name = "decalBlendMode", .type = RenderMaterialParameterType::Enum, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .defaultValueHint = "DISABLED", .description = "Decal blend mode." },
        { .name = "layerBlendMode", .type = RenderMaterialParameterType::Enum, .group = RenderMaterialParameterGroup::Advanced, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .defaultValueHint = "REPLACE", .description = "Layer blend mode." },
    }};

    static const std::array<RenderMaterialTextureSlotSchema, 13> textureSlots{{
        { .name = "Base Color", .assetIdFieldName = "albedoTextureAssetId", .pathFieldName = "albedoTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Base color / albedo texture.", .fallbackDescription = "White (1,1,1,1)" },
        { .name = "Normal", .assetIdFieldName = "normalTextureAssetId", .pathFieldName = "normalTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Tangent-space normal map.", .fallbackDescription = "Flat normal (0.5,0.5,1)" },
        { .name = "Metallic-Roughness", .assetIdFieldName = "metallicRoughnessTextureAssetId", .pathFieldName = "metallicRoughnessTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Metallic (B) and roughness (G) texture.", .fallbackDescription = "White (metallic=1, roughness=1)" },
        { .name = "Occlusion", .assetIdFieldName = "occlusionTextureAssetId", .pathFieldName = "occlusionTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Ambient occlusion texture.", .fallbackDescription = "White (occlusion=1)" },
        { .name = "Emissive", .assetIdFieldName = "emissiveTextureAssetId", .pathFieldName = "emissiveTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Emissive color texture.", .fallbackDescription = "White passthrough for emissiveColor * emissiveStrength" },
        { .name = "Clearcoat", .assetIdFieldName = "clearcoatTextureAssetId", .pathFieldName = "clearcoatTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Clearcoat intensity texture.", .fallbackDescription = "White" },
        { .name = "Clearcoat Roughness", .assetIdFieldName = "clearcoatRoughnessTextureAssetId", .pathFieldName = "clearcoatRoughnessTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Clearcoat roughness texture.", .fallbackDescription = "White" },
        { .name = "Sheen Color", .assetIdFieldName = "sheenColorTextureAssetId", .pathFieldName = "sheenColorTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Sheen color texture.", .fallbackDescription = "Black" },
        { .name = "Transmission", .assetIdFieldName = "transmissionTextureAssetId", .pathFieldName = "transmissionTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Transmission factor texture.", .fallbackDescription = "White" },
        { .name = "Thickness", .assetIdFieldName = "thicknessTextureAssetId", .pathFieldName = "thicknessTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Thickness texture.", .fallbackDescription = "White" },
        { .name = "Anisotropy", .assetIdFieldName = "anisotropyTextureAssetId", .pathFieldName = "anisotropyTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Anisotropy texture.", .fallbackDescription = "White" },
        { .name = "Decal", .assetIdFieldName = "decalTextureAssetId", .pathFieldName = "decalTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Decal texture.", .fallbackDescription = "White" },
        { .name = "Layer Mask", .assetIdFieldName = "layerMaskTextureAssetId", .pathFieldName = "layerMaskTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Layer mask texture.", .fallbackDescription = "White" },
    }};

    static const std::array<const char*, 3> alphaModes{
        "OPAQUE", "MASK", "BLEND"
    };

    static const std::array<const char*, 13> unsupportedAdvancedFeatures{
        "clearcoatFactor",
        "clearcoatRoughnessFactor",
        "sheenColor",
        "sheenRoughnessFactor",
        "transmissionFactor",
        "thicknessFactor",
        "attenuationColor",
        "attenuationDistance",
        "subsurfaceColor",
        "subsurfaceFactor",
        "anisotropyStrength",
        "anisotropyRotation",
        "tessellation",
    };

    static const std::array<RenderMaterialTypeMigrationOperation, 4> migrations{{
        { .kind = RenderMaterialTypeMigrationOperationKind::RenameParameter, .fromVersion = 0U, .toVersion = 1U, .field = "baseColorFactor", .targetField = "baseColor", .reason = "glTF naming was normalized to the engine PBR baseColor parameter." },
        { .kind = RenderMaterialTypeMigrationOperationKind::RenameParameter, .fromVersion = 0U, .toVersion = 1U, .field = "emissiveFactor", .targetField = "emissiveColor", .reason = "glTF naming was normalized to the engine PBR emissiveColor parameter." },
        { .kind = RenderMaterialTypeMigrationOperationKind::SetDefault, .fromVersion = 0U, .toVersion = 1U, .field = "emissiveStrength", .defaultValue = "1", .reason = "Legacy materials did not store emissive strength; runtime default is neutral." },
        { .kind = RenderMaterialTypeMigrationOperationKind::RemoveUnsupported, .fromVersion = 0U, .toVersion = 1U, .field = "specularGlossinessTexture", .reason = "KHR_materials_pbrSpecularGlossiness is not supported by the built-in metallic-roughness runtime." },
    }};

    static const RenderMaterialTypeSchema schema{
        .typeName = "builtin.pbr",
        .typeVersion = 1,
        .parameters = std::vector<RenderMaterialParameterSchema>(parameters.begin(), parameters.end()),
        .textureSlots = std::vector<RenderMaterialTextureSlotSchema>(textureSlots.begin(), textureSlots.end()),
        .alphaModes = std::vector<std::string>(alphaModes.begin(), alphaModes.end()),
        .unsupportedAdvancedFeatures = std::vector<std::string>(unsupportedAdvancedFeatures.begin(), unsupportedAdvancedFeatures.end()),
        .migrations = std::vector<RenderMaterialTypeMigrationOperation>(migrations.begin(), migrations.end()),
    };

    return schema;
}

} // namespace

const RenderMaterialTypeDocument& GetBuiltInPbrMaterialTypeDocument() noexcept {
    static const std::array<RenderMaterialTypeRenderPass, 6> renderPasses{{
        { .name = "BaseOpaque", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = "fs_mesh_instanced" },
        { .name = "GBuffer", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = "fs_mesh_gbuffer_instanced" },
        { .name = "ShadowDepth", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_shadow_instanced", .fragmentShader = "fs_mesh_shadow_instanced" },
        { .name = "SelectionId", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = "fs_mesh_instanced" },
        { .name = "EditorSelection", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = "fs_mesh_instanced" },
        { .name = "BaseTransparent", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = "fs_mesh_instanced" },
    }};

    static const std::array<RenderMaterialTypePermutationKey, 3> permutationKeys{{
        { .name = "alphaMode", .defaultValue = "OPAQUE", .allowedValues = std::vector<std::string>{ "OPAQUE", "MASK", "BLEND" } },
        { .name = "doubleSided", .defaultValue = "false", .allowedValues = std::vector<std::string>{ "false", "true" } },
        { .name = "textureSet", .defaultValue = "mvp-pbr", .allowedValues = std::vector<std::string>{ "mvp-pbr" } },
    }};

    static const std::array<RenderMaterialTypeRequiredResource, 10> requiredResources{{
        { .name = "vs_mesh_instanced", .kind = "vertexShader" },
        { .name = "fs_mesh_instanced", .kind = "fragmentShader" },
        { .name = "fs_mesh_gbuffer_instanced", .kind = "fragmentShader" },
        { .name = "vs_mesh_shadow_instanced", .kind = "vertexShader" },
        { .name = "fs_mesh_shadow_instanced", .kind = "fragmentShader" },
        { .name = "albedoTexture", .kind = "texture", .required = false },
        { .name = "normalTexture", .kind = "texture", .required = false },
        { .name = "metallicRoughnessTexture", .kind = "texture", .required = false },
        { .name = "occlusionTexture", .kind = "texture", .required = false },
        { .name = "emissiveTexture", .kind = "texture", .required = false },
    }};

    static const RenderMaterialTypeDocument document{
        .documentVersion = kRenderMaterialTypeDocumentVersion,
        .stableTypeId = "builtin.pbr",
        .version = 1U,
        .displayName = "Built-in PBR",
        .description = "Engine built-in metallic-roughness PBR material type.",
        .domain = RenderMaterialDomain::Surface,
        .shaderModel = RenderMaterialShaderModel::MetallicRoughnessPbr,
        .defaultBlendMode = RenderMaterialBlendMode::Opaque,
        .defaultCullMode = RenderMaterialCullMode::BackFace,
        .renderPasses = std::vector<RenderMaterialTypeRenderPass>(renderPasses.begin(), renderPasses.end()),
        .permutationKeys = std::vector<RenderMaterialTypePermutationKey>(permutationKeys.begin(), permutationKeys.end()),
        .requiredResources = std::vector<RenderMaterialTypeRequiredResource>(requiredResources.begin(), requiredResources.end()),
        .schema = BuildBuiltInPbrMaterialTypeSchema(),
    };
    return document;
}

const RenderMaterialTypeSchema& GetBuiltInPbrMaterialTypeSchema() noexcept {
    return GetBuiltInPbrMaterialTypeDocument().schema;
}

std::string_view RenderMaterialTypeDocumentDiagnosticCodeName(RenderMaterialTypeDocumentDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialTypeDocumentDiagnosticCode::FileOpenFailed:
        return "file_open_failed";
    case RenderMaterialTypeDocumentDiagnosticCode::InvalidDocumentVersion:
        return "invalid_document_version";
    case RenderMaterialTypeDocumentDiagnosticCode::UnsupportedDocumentVersion:
        return "unsupported_document_version";
    case RenderMaterialTypeDocumentDiagnosticCode::MissingStableTypeId:
        return "missing_stable_type_id";
    case RenderMaterialTypeDocumentDiagnosticCode::InvalidTypeVersion:
        return "invalid_type_version";
    case RenderMaterialTypeDocumentDiagnosticCode::UnknownField:
        return "unknown_field";
    case RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue:
        return "invalid_field_value";
    }
    return "unknown_diagnostic";
}

bool RenderMaterialTypeDocumentValidationResult::Succeeded() const noexcept {
    return diagnostics.empty();
}

bool RenderMaterialTypeDocumentParseResult::Succeeded() const noexcept {
    return document.has_value() && diagnostics.empty();
}

RenderMaterialTypeDocumentValidationResult ValidateRenderMaterialTypeDocument(const RenderMaterialTypeDocument& document) {
    RenderMaterialTypeDocumentValidationResult result{};
    if (document.documentVersion == 0U) {
        result.diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
            .code = RenderMaterialTypeDocumentDiagnosticCode::InvalidDocumentVersion,
            .message = "Material Type document version must be positive.",
        });
    } else if (document.documentVersion > kRenderMaterialTypeDocumentVersion) {
        result.diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
            .code = RenderMaterialTypeDocumentDiagnosticCode::UnsupportedDocumentVersion,
            .message = "Material Type document version " + std::to_string(document.documentVersion) + " is not supported.",
        });
    }
    if (document.stableTypeId.empty()) {
        result.diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
            .code = RenderMaterialTypeDocumentDiagnosticCode::MissingStableTypeId,
            .message = "Material Type document requires a stable type id.",
        });
    }
    if (document.version == 0U) {
        result.diagnostics.push_back(RenderMaterialTypeDocumentDiagnostic{
            .code = RenderMaterialTypeDocumentDiagnosticCode::InvalidTypeVersion,
            .message = "Material Type schema version must be positive.",
        });
    }
    return result;
}

RenderMaterialTypeDocumentParseResult ParseRenderMaterialTypeDocument(std::istream& input) {
    RenderMaterialTypeDocument document{};
    RenderMaterialTypeDocumentParseResult result{};
    bool sawContent = false;
    bool sawStableTypeId = false;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }
        sawContent = true;

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));

        if (keyword == "version") {
            std::uint32_t version = 0U;
            if (!ParseUInt32(rest, version) || version == 0U) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidDocumentVersion, lineNumber, "version", "Invalid Material Type document version.", std::string{ rest });
                continue;
            }
            if (version > kRenderMaterialTypeDocumentVersion) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::UnsupportedDocumentVersion, lineNumber, "version", "Unsupported Material Type document version " + std::to_string(version) + ".", std::string{ rest });
                continue;
            }
            document.documentVersion = version;
            continue;
        }
        if (keyword == "stableTypeId") {
            if (rest.empty()) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::MissingStableTypeId, lineNumber, "stableTypeId", "Material Type stable id is required.");
                continue;
            }
            document.stableTypeId = std::string{ rest };
            sawStableTypeId = true;
            document.schema.typeName = document.stableTypeId;
            continue;
        }
        if (keyword == "typeVersion") {
            std::uint32_t version = 0U;
            if (!ParseUInt32(rest, version) || version == 0U) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidTypeVersion, lineNumber, "typeVersion", "Invalid Material Type schema version.", std::string{ rest });
                continue;
            }
            document.version = version;
            document.schema.typeVersion = version;
            continue;
        }
        if (keyword == "displayName") {
            document.displayName = std::string{ rest };
            continue;
        }
        if (keyword == "description") {
            document.description = std::string{ rest };
            continue;
        }
        if (keyword == "domain") {
            if (!ParseDomain(rest, document.domain)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "domain", "Unsupported Material Type domain.", std::string{ rest });
            }
            continue;
        }
        if (keyword == "shaderModel") {
            if (!ParseShaderModel(rest, document.shaderModel)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "shaderModel", "Unsupported Material Type shader model.", std::string{ rest });
            }
            continue;
        }
        if (keyword == "defaultBlendMode") {
            if (!ParseBlendMode(rest, document.defaultBlendMode)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "defaultBlendMode", "Unsupported Material Type blend mode.", std::string{ rest });
            }
            continue;
        }
        if (keyword == "defaultCullMode") {
            if (!ParseCullMode(rest, document.defaultCullMode)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "defaultCullMode", "Unsupported Material Type cull mode.", std::string{ rest });
            }
            continue;
        }
        if (keyword == "alphaMode") {
            if (!rest.empty()) {
                document.schema.alphaModes.emplace_back(rest);
            }
            continue;
        }
        if (keyword == "unsupportedFeature") {
            if (!rest.empty()) {
                document.schema.unsupportedAdvancedFeatures.emplace_back(rest);
            }
            continue;
        }
        if (keyword == "renderPass") {
            std::istringstream stream{ std::string{ rest } };
            std::string name;
            std::string supportText;
            std::string vertexShader;
            std::string fragmentShader;
            RenderMaterialFeatureSupport support{};
            if (!(stream >> name >> supportText >> vertexShader >> fragmentShader) || !ParseFeatureSupport(supportText, support)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "renderPass", "Invalid Material Type render pass.", std::string{ rest });
                continue;
            }
            document.renderPasses.push_back(RenderMaterialTypeRenderPass{
                .name = DecodeToken(name),
                .support = support,
                .vertexShader = DecodeToken(vertexShader),
                .fragmentShader = DecodeToken(fragmentShader),
            });
            continue;
        }
        if (keyword == "permutation") {
            std::istringstream stream{ std::string{ rest } };
            std::string name;
            std::string defaultValue;
            std::string allowed;
            if (!(stream >> name >> defaultValue >> allowed)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "permutation", "Invalid Material Type permutation key.", std::string{ rest });
                continue;
            }
            std::vector<std::string> allowedValues = SplitAllowedValues(allowed);
            for (std::string& value : allowedValues) {
                value = DecodeToken(value);
            }
            if (allowedValues.empty()) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "permutation", "Material Type permutation key requires allowed values.", std::string{ rest });
                continue;
            }
            document.permutationKeys.push_back(RenderMaterialTypePermutationKey{
                .name = DecodeToken(name),
                .defaultValue = DecodeToken(defaultValue),
                .allowedValues = std::move(allowedValues),
            });
            continue;
        }
        if (keyword == "requiredResource") {
            std::istringstream stream{ std::string{ rest } };
            std::string name;
            std::string kind;
            std::string requiredText;
            bool required = true;
            if (!(stream >> name >> kind >> requiredText) || !ParseBool(requiredText, required)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "requiredResource", "Invalid Material Type required resource.", std::string{ rest });
                continue;
            }
            document.requiredResources.push_back(RenderMaterialTypeRequiredResource{
                .name = DecodeToken(name),
                .kind = DecodeToken(kind),
                .required = required,
            });
            continue;
        }
        if (keyword == "parameter") {
            std::istringstream stream{ std::string{ rest } };
            std::string name;
            std::string typeText;
            std::string groupText;
            std::string supportText;
            std::string rangeMinText;
            std::string rangeMaxText;
            std::string defaultValue;
            std::string displayName;
            std::string description;
            std::string editorOrderText;
            std::string overrideText;
            RenderMaterialParameterType type{};
            RenderMaterialParameterGroup group{};
            RenderMaterialFeatureSupport support{};
            if (!(stream >> name >> typeText >> groupText >> supportText >> rangeMinText >> rangeMaxText >> defaultValue) ||
                !ParseParameterType(typeText, type) ||
                !ParseParameterGroup(groupText, group) ||
                !ParseFeatureSupport(supportText, support)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "parameter", "Invalid Material Type parameter schema row.", std::string{ rest });
                continue;
            }
            std::optional<RenderMaterialParameterRange> range;
            if (rangeMinText != "_" || rangeMaxText != "_") {
                float min = 0.0F;
                float max = 0.0F;
                if (!ParseFloat(rangeMinText, min) || !ParseFloat(rangeMaxText, max)) {
                    AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "parameter", "Invalid Material Type parameter range.", std::string{ rest });
                    continue;
                }
                range = RenderMaterialParameterRange{ min, max };
            }
            std::uint32_t editorOrder = 0U;
            bool overrideSupported = true;
            if (stream >> displayName) {
                if (stream >> description) {
                    if (stream >> editorOrderText && !ParseUInt32(editorOrderText, editorOrder)) {
                        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "parameter", "Invalid Material Type parameter editor order.", std::string{ rest });
                        continue;
                    }
                    if (stream >> overrideText && !ParseBool(overrideText, overrideSupported)) {
                        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "parameter", "Invalid Material Type parameter override support flag.", std::string{ rest });
                        continue;
                    }
                }
            }
            document.schema.parameters.push_back(RenderMaterialParameterSchema{
                .name = DecodeToken(name),
                .displayName = displayName == "_" ? std::string{} : DecodeToken(displayName),
                .type = type,
                .group = group,
                .runtimeSupport = support,
                .range = range,
                .defaultValueHint = defaultValue == "_" ? std::string{} : DecodeToken(defaultValue),
                .description = description == "_" ? std::string{} : DecodeToken(description),
                .overrideSupported = overrideSupported,
                .editorOrder = editorOrder,
            });
            continue;
        }
        if (keyword == "textureSlot") {
            std::istringstream stream{ std::string{ rest } };
            std::string name;
            std::string assetIdField;
            std::string pathField;
            std::string colorSpaceText;
            std::string supportText;
            std::string description;
            std::string fallback;
            std::string editorOrderText;
            std::string role;
            std::string overrideText;
            std::string stableId;
            RenderMaterialTextureColorSpace colorSpace{};
            RenderMaterialFeatureSupport support{};
            if (!(stream >> name >> assetIdField >> pathField >> colorSpaceText >> supportText) ||
                !ParseTextureColorSpace(colorSpaceText, colorSpace) ||
                !ParseFeatureSupport(supportText, support)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "textureSlot", "Invalid Material Type texture slot schema row.", std::string{ rest });
                continue;
            }
            std::uint32_t editorOrder = 0U;
            bool overrideSupported = true;
            if (stream >> description) {
                if (stream >> fallback) {
                    if (stream >> editorOrderText && !ParseUInt32(editorOrderText, editorOrder)) {
                        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "textureSlot", "Invalid Material Type texture slot editor order.", std::string{ rest });
                        continue;
                    }
                    if (stream >> role) {
                        if (stream >> overrideText && !ParseBool(overrideText, overrideSupported)) {
                            AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "textureSlot", "Invalid Material Type texture slot override support flag.", std::string{ rest });
                            continue;
                        }
                        static_cast<void>(stream >> stableId);
                    }
                }
            }
            document.schema.textureSlots.push_back(RenderMaterialTextureSlotSchema{
                .name = DecodeToken(name),
                .stableId = stableId.empty() || stableId == "_" ? std::string{} : DecodeToken(stableId),
                .role = role == "_" ? std::string{} : DecodeToken(role),
                .assetIdFieldName = DecodeToken(assetIdField),
                .pathFieldName = DecodeToken(pathField),
                .expectedColorSpace = colorSpace,
                .runtimeSupport = support,
                .description = description == "_" ? std::string{} : DecodeToken(description),
                .fallbackDescription = fallback == "_" ? std::string{} : DecodeToken(fallback),
                .overrideSupported = overrideSupported,
                .editorOrder = editorOrder,
            });
            continue;
        }
        if (keyword == "migration") {
            std::istringstream stream{ std::string{ rest } };
            std::string kindText;
            std::string fromVersionText;
            std::string toVersionText;
            std::string field;
            std::string targetField;
            std::string defaultValue;
            std::string reason;
            RenderMaterialTypeMigrationOperationKind kind{};
            std::uint32_t fromVersion = 0U;
            std::uint32_t toVersion = 0U;
            if (!(stream >> kindText >> fromVersionText >> toVersionText >> field >> targetField >> defaultValue >> reason) ||
                !ParseMigrationOperationKind(kindText, kind) ||
                !ParseUInt32(fromVersionText, fromVersion) ||
                !ParseUInt32(toVersionText, toVersion)) {
                AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::InvalidFieldValue, lineNumber, "migration", "Invalid Material Type migration row.", std::string{ rest });
                continue;
            }
            document.schema.migrations.push_back(RenderMaterialTypeMigrationOperation{
                .kind = kind,
                .fromVersion = fromVersion,
                .toVersion = toVersion,
                .field = DecodeToken(field),
                .targetField = targetField == "_" ? std::string{} : DecodeToken(targetField),
                .defaultValue = defaultValue == "_" ? std::string{} : DecodeToken(defaultValue),
                .reason = reason == "_" ? std::string{} : DecodeToken(reason),
            });
            continue;
        }

        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::UnknownField, lineNumber, std::string{ keyword }, "Unknown Material Type field '" + std::string{ keyword } + "'.", std::string{ trimmed });
    }

    if (!sawContent) {
        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::MissingStableTypeId, 0U, {}, "Material Type document is empty.");
    }
    if (!sawStableTypeId) {
        AddDiagnostic(result.diagnostics, RenderMaterialTypeDocumentDiagnosticCode::MissingStableTypeId, 0U, "stableTypeId", "Material Type stable id is required.");
    }
    const RenderMaterialTypeDocumentValidationResult validation = ValidateRenderMaterialTypeDocument(document);
    for (const RenderMaterialTypeDocumentDiagnostic& diagnostic : validation.diagnostics) {
        result.diagnostics.push_back(diagnostic);
    }
    if (result.diagnostics.empty()) {
        result.document = std::move(document);
    }
    return result;
}

void WriteRenderMaterialTypeDocument(std::ostream& output, const RenderMaterialTypeDocument& document) {
    output << "# KB material type\n";
    output << "version " << (document.documentVersion == 0U ? kRenderMaterialTypeDocumentVersion : document.documentVersion) << '\n';
    output << "stableTypeId " << document.stableTypeId << '\n';
    output << "typeVersion " << document.version << '\n';
    if (!document.displayName.empty()) {
        output << "displayName " << document.displayName << '\n';
    }
    if (!document.description.empty()) {
        output << "description " << document.description << '\n';
    }
    output << "domain " << DomainName(document.domain) << '\n';
    output << "shaderModel " << ShaderModelName(document.shaderModel) << '\n';
    output << "defaultBlendMode " << BlendModeName(document.defaultBlendMode) << '\n';
    output << "defaultCullMode " << CullModeName(document.defaultCullMode) << '\n';
    for (const std::string& alphaMode : document.schema.alphaModes) {
        output << "alphaMode " << alphaMode << '\n';
    }
    for (const std::string& feature : document.schema.unsupportedAdvancedFeatures) {
        output << "unsupportedFeature " << feature << '\n';
    }
    for (const RenderMaterialTypeRenderPass& pass : document.renderPasses) {
        output << "renderPass " << EncodeToken(pass.name) << ' ' << FeatureSupportName(pass.support) << ' ' << EncodeToken(pass.vertexShader) << ' ' << EncodeToken(pass.fragmentShader) << '\n';
    }
    for (const RenderMaterialTypePermutationKey& permutation : document.permutationKeys) {
        output << "permutation " << EncodeToken(permutation.name) << ' ' << EncodeToken(permutation.defaultValue) << ' ' << EncodeToken(JoinAllowedValues(permutation.allowedValues)) << '\n';
    }
    for (const RenderMaterialTypeRequiredResource& resource : document.requiredResources) {
        output << "requiredResource " << EncodeToken(resource.name) << ' ' << EncodeToken(resource.kind) << ' ' << (resource.required ? "true" : "false") << '\n';
    }
    for (const RenderMaterialParameterSchema& parameter : document.schema.parameters) {
        output << "parameter " << EncodeToken(parameter.name) << ' '
            << ParameterTypeName(parameter.type) << ' '
            << ParameterGroupName(parameter.group) << ' '
            << FeatureSupportName(parameter.runtimeSupport) << ' ';
        if (parameter.range.has_value()) {
            output << parameter.range->min << ' ' << parameter.range->max;
        } else {
            output << "_ _";
        }
        output << ' ' << (parameter.defaultValueHint.empty() ? "_" : EncodeToken(parameter.defaultValueHint))
            << ' ' << (parameter.displayName.empty() ? "_" : EncodeToken(parameter.displayName))
            << ' ' << (parameter.description.empty() ? "_" : EncodeToken(parameter.description))
            << ' ' << parameter.editorOrder
            << ' ' << (parameter.overrideSupported ? "true" : "false")
            << '\n';
    }
    for (const RenderMaterialTextureSlotSchema& slot : document.schema.textureSlots) {
        output << "textureSlot " << EncodeToken(slot.name) << ' '
            << EncodeToken(slot.assetIdFieldName) << ' '
            << EncodeToken(slot.pathFieldName) << ' '
            << TextureColorSpaceName(slot.expectedColorSpace) << ' '
            << FeatureSupportName(slot.runtimeSupport) << ' '
            << (slot.description.empty() ? "_" : EncodeToken(slot.description)) << ' '
            << (slot.fallbackDescription.empty() ? "_" : EncodeToken(slot.fallbackDescription)) << ' '
            << slot.editorOrder
            << ' ' << (slot.role.empty() ? "_" : EncodeToken(slot.role))
            << ' ' << (slot.overrideSupported ? "true" : "false");
        if (!slot.stableId.empty()) {
            output << ' ' << EncodeToken(slot.stableId);
        }
        output << '\n';
    }
    for (const RenderMaterialTypeMigrationOperation& migration : document.schema.migrations) {
        output << "migration " << MigrationOperationKindName(migration.kind) << ' '
            << migration.fromVersion << ' '
            << migration.toVersion << ' '
            << EncodeToken(migration.field) << ' '
            << (migration.targetField.empty() ? "_" : EncodeToken(migration.targetField)) << ' '
            << (migration.defaultValue.empty() ? "_" : EncodeToken(migration.defaultValue)) << ' '
            << (migration.reason.empty() ? "_" : EncodeToken(migration.reason))
            << '\n';
    }
}

const RenderMaterialParameterSchema* FindMaterialParameterSchema(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept {
    for (const auto& param : schema.parameters) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

const RenderMaterialTextureSlotSchema* FindMaterialTextureSlotSchema(
    const RenderMaterialTypeSchema& schema, std::string_view assetIdFieldName) noexcept {
    for (const auto& slot : schema.textureSlots) {
        if (slot.assetIdFieldName == assetIdFieldName) {
            return &slot;
        }
    }
    return nullptr;
}

bool IsMaterialTexturePathField(
    const RenderMaterialTypeSchema& schema, std::string_view name) noexcept {
    for (const auto& slot : schema.textureSlots) {
        if (slot.pathFieldName == name) {
            return true;
        }
    }
    return false;
}

const RenderMaterialTypeMigrationOperation* FindMaterialTypeMigration(
    const RenderMaterialTypeSchema& schema,
    RenderMaterialTypeMigrationOperationKind kind,
    std::string_view field) noexcept {
    for (const RenderMaterialTypeMigrationOperation& migration : schema.migrations) {
        if (migration.kind == kind && migration.field == field) {
            return &migration;
        }
    }
    return nullptr;
}

} // namespace kb::render
