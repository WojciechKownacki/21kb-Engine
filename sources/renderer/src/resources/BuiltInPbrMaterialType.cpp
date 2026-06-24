#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <array>

namespace kb::render {
namespace {

const RenderMaterialTypeSchema& BuildBuiltInPbrMaterialTypeSchema() {
    static const std::array<RenderMaterialParameterSchema, 28> parameters{{
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
        { .name = "alphaMode", .type = RenderMaterialParameterType::Enum, .group = RenderMaterialParameterGroup::Core, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .defaultValueHint = "OPAQUE", .description = "Alpha mode: OPAQUE, MASK, or BLEND." },
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
        { .name = "Emissive", .assetIdFieldName = "emissiveTextureAssetId", .pathFieldName = "emissiveTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::Supported, .description = "Emissive color texture.", .fallbackDescription = "Black (0,0,0)" },
        { .name = "Clearcoat", .assetIdFieldName = "clearcoatTextureAssetId", .pathFieldName = "clearcoatTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Clearcoat intensity texture.", .fallbackDescription = "White" },
        { .name = "Clearcoat Roughness", .assetIdFieldName = "clearcoatRoughnessTextureAssetId", .pathFieldName = "clearcoatRoughnessTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Clearcoat roughness texture.", .fallbackDescription = "White" },
        { .name = "Sheen Color", .assetIdFieldName = "sheenColorTextureAssetId", .pathFieldName = "sheenColorTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Sheen color texture.", .fallbackDescription = "Black" },
        { .name = "Transmission", .assetIdFieldName = "transmissionTextureAssetId", .pathFieldName = "transmissionTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Transmission factor texture.", .fallbackDescription = "White" },
        { .name = "Thickness", .assetIdFieldName = "thicknessTextureAssetId", .pathFieldName = "thicknessTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Thickness texture.", .fallbackDescription = "White" },
        { .name = "Anisotropy", .assetIdFieldName = "anisotropyTextureAssetId", .pathFieldName = "anisotropyTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Anisotropy texture.", .fallbackDescription = "White" },
        { .name = "Decal", .assetIdFieldName = "decalTextureAssetId", .pathFieldName = "decalTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Decal texture.", .fallbackDescription = "White" },
        { .name = "Layer Mask", .assetIdFieldName = "layerMaskTextureAssetId", .pathFieldName = "layerMaskTexture", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored, .description = "Layer mask texture.", .fallbackDescription = "White" },
    }};

    static const std::array<std::string_view, 3> alphaModes{
        "OPAQUE", "MASK", "BLEND"
    };

    static const std::array<std::string_view, 12> unsupportedAdvancedFeatures{
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
    };

    static const RenderMaterialTypeSchema schema{
        .typeName = "builtin.pbr",
        .typeVersion = 1,
        .parameters = std::vector<RenderMaterialParameterSchema>(parameters.begin(), parameters.end()),
        .textureSlots = std::vector<RenderMaterialTextureSlotSchema>(textureSlots.begin(), textureSlots.end()),
        .alphaModes = std::vector<std::string_view>(alphaModes.begin(), alphaModes.end()),
        .unsupportedAdvancedFeatures = std::vector<std::string_view>(unsupportedAdvancedFeatures.begin(), unsupportedAdvancedFeatures.end()),
    };

    return schema;
}

} // namespace

const RenderMaterialTypeSchema& GetBuiltInPbrMaterialTypeSchema() noexcept {
    return BuildBuiltInPbrMaterialTypeSchema();
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

} // namespace kb::render
