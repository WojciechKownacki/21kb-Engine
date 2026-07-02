#include "kb/render/resources/RenderMaterialAssetWriter.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <string_view>

namespace kb::render {
namespace {

class IRenderMaterialAssetPropertyWriter {
public:
    virtual ~IRenderMaterialAssetPropertyWriter() = default;
    virtual void Write(std::ostream& output, const RenderMaterialAssetData& asset) const = 0;
};

void WriteFloat(std::ostream& output, float value) {
    output << std::setprecision(9) << value;
}

void WriteVec3(std::ostream& output, const char* name, const float (&values)[3]) {
    output << name << ' ';
    WriteFloat(output, values[0]);
    output << ' ';
    WriteFloat(output, values[1]);
    output << ' ';
    WriteFloat(output, values[2]);
    output << '\n';
}

void WriteVec2(std::ostream& output, const char* name, const float (&values)[2]) {
    output << name << ' ';
    WriteFloat(output, values[0]);
    output << ' ';
    WriteFloat(output, values[1]);
    output << '\n';
}

void WriteScalar(std::ostream& output, const char* name, float value) {
    output << name << ' ';
    WriteFloat(output, value);
    output << '\n';
}

void WriteTextureAssetId(std::ostream& output, const char* name, std::uint64_t assetId) {
    if (assetId != 0U) {
        output << name << ' ' << assetId << '\n';
    }
}

void WriteTexturePath(std::ostream& output, const char* name, std::string_view path) {
    if (!path.empty()) {
        output << name << ' ' << path << '\n';
    }
}

void WriteGraph(std::ostream& output, const RenderMaterialGraphDocument& graph) {
    if (graph.nodes.empty() && graph.links.empty()) {
        return;
    }
    WriteRenderMaterialGraphDocument(output, graph);
}

[[nodiscard]] const char* MaterialParameterTypeName(RenderMaterialParameterType type) noexcept {
    switch (type) {
    case RenderMaterialParameterType::Scalar:
        return "Scalar";
    case RenderMaterialParameterType::Vec3:
        return "Vec3";
    case RenderMaterialParameterType::Vec4:
        return "Vec4";
    case RenderMaterialParameterType::Color:
        return "Color";
    case RenderMaterialParameterType::Enum:
        return "Enum";
    case RenderMaterialParameterType::Bool:
        return "Bool";
    case RenderMaterialParameterType::Texture:
        return "Texture";
    }
    return "Scalar";
}

void WriteGraphParameterValues(std::ostream& output, const RenderMaterialAssetData& asset) {
    for (const RenderMaterialGraphParameterValue& value : asset.graphParameterValues) {
        if (value.stableId.empty()) {
            continue;
        }
        output << "graphParameterValue " << value.stableId << ' ' << MaterialParameterTypeName(value.type);
        switch (value.type) {
        case RenderMaterialParameterType::Scalar:
            output << ' ';
            WriteFloat(output, value.numbers[0]);
            break;
        case RenderMaterialParameterType::Vec3:
            output << ' ';
            WriteFloat(output, value.numbers[0]);
            output << ' ';
            WriteFloat(output, value.numbers[1]);
            output << ' ';
            WriteFloat(output, value.numbers[2]);
            break;
        case RenderMaterialParameterType::Vec4:
        case RenderMaterialParameterType::Color:
            output << ' ';
            WriteFloat(output, value.numbers[0]);
            output << ' ';
            WriteFloat(output, value.numbers[1]);
            output << ' ';
            WriteFloat(output, value.numbers[2]);
            output << ' ';
            WriteFloat(output, value.numbers[3]);
            break;
        case RenderMaterialParameterType::Bool:
            output << ' ' << (value.boolValue ? "true" : "false");
            break;
        case RenderMaterialParameterType::Enum:
            output << ' ' << (value.text.empty() ? "_" : value.text);
            break;
        case RenderMaterialParameterType::Texture:
            output << ' ' << value.assetId;
            break;
        }
        output << '\n';
    }
}

[[nodiscard]] const char* AlphaModeName(RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case RenderMaterialAlphaMode::Opaque:
        return "OPAQUE";
    case RenderMaterialAlphaMode::Mask:
        return "MASK";
    case RenderMaterialAlphaMode::Blend:
        return "BLEND";
    }
    return "OPAQUE";
}

[[nodiscard]] const char* DecalBlendModeName(RenderMaterialDecalBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialDecalBlendMode::Disabled:
        return "DISABLED";
    case RenderMaterialDecalBlendMode::BaseColor:
        return "BASE_COLOR";
    case RenderMaterialDecalBlendMode::Normal:
        return "NORMAL";
    case RenderMaterialDecalBlendMode::Pbr:
        return "PBR";
    }
    return "DISABLED";
}

[[nodiscard]] const char* LayerBlendModeName(RenderMaterialLayerBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialLayerBlendMode::Replace:
        return "REPLACE";
    case RenderMaterialLayerBlendMode::Add:
        return "ADD";
    case RenderMaterialLayerBlendMode::Multiply:
        return "MULTIPLY";
    }
    return "REPLACE";
}

[[nodiscard]] const char* TranslucencyBlendName(RenderMaterialTranslucencyBlend mode) noexcept {
    switch (mode) {
    case RenderMaterialTranslucencyBlend::Alpha:
        return "ALPHA";
    case RenderMaterialTranslucencyBlend::Additive:
        return "ADDITIVE";
    case RenderMaterialTranslucencyBlend::Modulate:
        return "MODULATE";
    case RenderMaterialTranslucencyBlend::PreMultipliedAlpha:
        return "PREMULTIPLIED_ALPHA";
    case RenderMaterialTranslucencyBlend::AlphaHoldout:
        return "ALPHA_HOLDOUT";
    }
    return "ALPHA";
}

class BaseColorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "baseColor ";
        WriteFloat(output, asset.desc.baseColor[0]);
        output << ' ';
        WriteFloat(output, asset.desc.baseColor[1]);
        output << ' ';
        WriteFloat(output, asset.desc.baseColor[2]);
        output << ' ';
        WriteFloat(output, asset.desc.baseColor[3]);
        output << '\n';
    }
};

class EmissiveColorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec3(output, "emissiveColor", asset.desc.emissiveColor);
    }
};

class MetallicFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "metallicFactor", asset.desc.metallicFactor);
    }
};

class RoughnessFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "roughnessFactor", asset.desc.roughnessFactor);
    }
};

class NormalScalePropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "normalScale", asset.desc.normalScale);
    }
};

class OcclusionStrengthPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "occlusionStrength", asset.desc.occlusionStrength);
    }
};

class EmissiveStrengthPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "emissiveStrength", asset.desc.emissiveStrength);
    }
};

class AlphaCutoffPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "alphaCutoff", asset.desc.alphaCutoff);
    }
};

class ClearcoatFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "clearcoatFactor", asset.desc.clearcoatFactor);
    }
};

class ClearcoatRoughnessFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "clearcoatRoughnessFactor", asset.desc.clearcoatRoughnessFactor);
    }
};

class SheenColorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec3(output, "sheenColor", asset.desc.sheenColor);
    }
};

class SheenRoughnessFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "sheenRoughnessFactor", asset.desc.sheenRoughnessFactor);
    }
};

class TransmissionFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "transmissionFactor", asset.desc.transmissionFactor);
    }
};

class ThicknessFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "thicknessFactor", asset.desc.thicknessFactor);
    }
};

class AttenuationColorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec3(output, "attenuationColor", asset.desc.attenuationColor);
    }
};

class AttenuationDistancePropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "attenuationDistance", asset.desc.attenuationDistance);
    }
};

class SubsurfaceColorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec3(output, "subsurfaceColor", asset.desc.subsurfaceColor);
    }
};

class SubsurfaceFactorPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "subsurfaceFactor", asset.desc.subsurfaceFactor);
    }
};

class AnisotropyStrengthPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "anisotropyStrength", asset.desc.anisotropyStrength);
    }
};

class AnisotropyRotationPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "anisotropyRotation", asset.desc.anisotropyRotation);
    }
};

class LayerWeightPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteScalar(output, "layerWeight", asset.desc.layerWeight);
    }
};

class AlphaModePropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "alphaMode " << AlphaModeName(asset.desc.alphaMode) << '\n';
    }
};

class DecalBlendModePropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "decalBlendMode " << DecalBlendModeName(asset.desc.decalBlendMode) << '\n';
    }
};

class LayerBlendModePropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "layerBlendMode " << LayerBlendModeName(asset.desc.layerBlendMode) << '\n';
    }
};

class DoubleSidedPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "doubleSided " << (asset.desc.doubleSided ? "true" : "false") << '\n';
    }
};

class TranslucencyBlendPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "translucencyBlend " << TranslucencyBlendName(asset.desc.translucencyBlend) << '\n';
    }
};

class WritesDepthPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        output << "writesDepth " << (asset.desc.writesDepth ? "true" : "false") << '\n';
    }
};

class UvTilingPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec2(output, "tiling", asset.desc.uvTiling);
    }
};

class UvOffsetPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteVec2(output, "offset", asset.desc.uvOffset);
    }
};

class AlbedoTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "albedoTextureAssetId", asset.desc.albedoTextureAssetId);
    }
};

class NormalTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "normalTextureAssetId", asset.desc.normalTextureAssetId);
    }
};

class MetallicRoughnessTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "metallicRoughnessTextureAssetId", asset.desc.metallicRoughnessTextureAssetId);
    }
};

class OcclusionTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "occlusionTextureAssetId", asset.desc.occlusionTextureAssetId);
    }
};

class EmissiveTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "emissiveTextureAssetId", asset.desc.emissiveTextureAssetId);
    }
};

class ClearcoatTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "clearcoatTextureAssetId", asset.desc.clearcoatTextureAssetId);
    }
};

class ClearcoatRoughnessTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "clearcoatRoughnessTextureAssetId", asset.desc.clearcoatRoughnessTextureAssetId);
    }
};

class SheenColorTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "sheenColorTextureAssetId", asset.desc.sheenColorTextureAssetId);
    }
};

class TransmissionTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "transmissionTextureAssetId", asset.desc.transmissionTextureAssetId);
    }
};

class ThicknessTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "thicknessTextureAssetId", asset.desc.thicknessTextureAssetId);
    }
};

class AnisotropyTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "anisotropyTextureAssetId", asset.desc.anisotropyTextureAssetId);
    }
};

class DecalTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "decalTextureAssetId", asset.desc.decalTextureAssetId);
    }
};

class LayerMaskTextureAssetIdPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTextureAssetId(output, "layerMaskTextureAssetId", asset.desc.layerMaskTextureAssetId);
    }
};

class AlbedoTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "albedoTexture", asset.albedoTexturePath);
    }
};

class NormalTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "normalTexture", asset.normalTexturePath);
    }
};

class MetallicRoughnessTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "metallicRoughnessTexture", asset.metallicRoughnessTexturePath);
    }
};

class OcclusionTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "occlusionTexture", asset.occlusionTexturePath);
    }
};

class EmissiveTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "emissiveTexture", asset.emissiveTexturePath);
    }
};

class ClearcoatTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "clearcoatTexture", asset.clearcoatTexturePath);
    }
};

class ClearcoatRoughnessTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "clearcoatRoughnessTexture", asset.clearcoatRoughnessTexturePath);
    }
};

class SheenColorTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "sheenColorTexture", asset.sheenColorTexturePath);
    }
};

class TransmissionTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "transmissionTexture", asset.transmissionTexturePath);
    }
};

class ThicknessTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "thicknessTexture", asset.thicknessTexturePath);
    }
};

class AnisotropyTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "anisotropyTexture", asset.anisotropyTexturePath);
    }
};

class DecalTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "decalTexture", asset.decalTexturePath);
    }
};

class LayerMaskTexturePathPropertyWriter final : public IRenderMaterialAssetPropertyWriter {
public:
    void Write(std::ostream& output, const RenderMaterialAssetData& asset) const override {
        WriteTexturePath(output, "layerMaskTexture", asset.layerMaskTexturePath);
    }
};

const std::array<const IRenderMaterialAssetPropertyWriter*, 55>& PropertyWriters() {
    static const BaseColorPropertyWriter baseColor;
    static const EmissiveColorPropertyWriter emissiveColor;
    static const MetallicFactorPropertyWriter metallicFactor;
    static const RoughnessFactorPropertyWriter roughnessFactor;
    static const NormalScalePropertyWriter normalScale;
    static const OcclusionStrengthPropertyWriter occlusionStrength;
    static const EmissiveStrengthPropertyWriter emissiveStrength;
    static const AlphaCutoffPropertyWriter alphaCutoff;
    static const ClearcoatFactorPropertyWriter clearcoatFactor;
    static const ClearcoatRoughnessFactorPropertyWriter clearcoatRoughnessFactor;
    static const SheenColorPropertyWriter sheenColor;
    static const SheenRoughnessFactorPropertyWriter sheenRoughnessFactor;
    static const TransmissionFactorPropertyWriter transmissionFactor;
    static const ThicknessFactorPropertyWriter thicknessFactor;
    static const AttenuationColorPropertyWriter attenuationColor;
    static const AttenuationDistancePropertyWriter attenuationDistance;
    static const SubsurfaceColorPropertyWriter subsurfaceColor;
    static const SubsurfaceFactorPropertyWriter subsurfaceFactor;
    static const AnisotropyStrengthPropertyWriter anisotropyStrength;
    static const AnisotropyRotationPropertyWriter anisotropyRotation;
    static const LayerWeightPropertyWriter layerWeight;
    static const AlphaModePropertyWriter alphaMode;
    static const DecalBlendModePropertyWriter decalBlendMode;
    static const LayerBlendModePropertyWriter layerBlendMode;
    static const TranslucencyBlendPropertyWriter translucencyBlend;
    static const DoubleSidedPropertyWriter doubleSided;
    static const WritesDepthPropertyWriter writesDepth;
    static const UvTilingPropertyWriter uvTiling;
    static const UvOffsetPropertyWriter uvOffset;
    static const AlbedoTextureAssetIdPropertyWriter albedoTextureAssetId;
    static const NormalTextureAssetIdPropertyWriter normalTextureAssetId;
    static const MetallicRoughnessTextureAssetIdPropertyWriter metallicRoughnessTextureAssetId;
    static const OcclusionTextureAssetIdPropertyWriter occlusionTextureAssetId;
    static const EmissiveTextureAssetIdPropertyWriter emissiveTextureAssetId;
    static const ClearcoatTextureAssetIdPropertyWriter clearcoatTextureAssetId;
    static const ClearcoatRoughnessTextureAssetIdPropertyWriter clearcoatRoughnessTextureAssetId;
    static const SheenColorTextureAssetIdPropertyWriter sheenColorTextureAssetId;
    static const TransmissionTextureAssetIdPropertyWriter transmissionTextureAssetId;
    static const ThicknessTextureAssetIdPropertyWriter thicknessTextureAssetId;
    static const AnisotropyTextureAssetIdPropertyWriter anisotropyTextureAssetId;
    static const DecalTextureAssetIdPropertyWriter decalTextureAssetId;
    static const LayerMaskTextureAssetIdPropertyWriter layerMaskTextureAssetId;
    static const AlbedoTexturePathPropertyWriter albedoTexturePath;
    static const NormalTexturePathPropertyWriter normalTexturePath;
    static const MetallicRoughnessTexturePathPropertyWriter metallicRoughnessTexturePath;
    static const OcclusionTexturePathPropertyWriter occlusionTexturePath;
    static const EmissiveTexturePathPropertyWriter emissiveTexturePath;
    static const ClearcoatTexturePathPropertyWriter clearcoatTexturePath;
    static const ClearcoatRoughnessTexturePathPropertyWriter clearcoatRoughnessTexturePath;
    static const SheenColorTexturePathPropertyWriter sheenColorTexturePath;
    static const TransmissionTexturePathPropertyWriter transmissionTexturePath;
    static const ThicknessTexturePathPropertyWriter thicknessTexturePath;
    static const AnisotropyTexturePathPropertyWriter anisotropyTexturePath;
    static const DecalTexturePathPropertyWriter decalTexturePath;
    static const LayerMaskTexturePathPropertyWriter layerMaskTexturePath;

    static const std::array<const IRenderMaterialAssetPropertyWriter*, 55> writers{
        &baseColor,
        &metallicFactor,
        &roughnessFactor,
        &normalScale,
        &occlusionStrength,
        &emissiveColor,
        &emissiveStrength,
        &alphaMode,
        &alphaCutoff,
        &translucencyBlend,
        &doubleSided,
        &writesDepth,
        &uvTiling,
        &uvOffset,
        &albedoTextureAssetId,
        &normalTextureAssetId,
        &metallicRoughnessTextureAssetId,
        &occlusionTextureAssetId,
        &emissiveTextureAssetId,
        &albedoTexturePath,
        &normalTexturePath,
        &metallicRoughnessTexturePath,
        &occlusionTexturePath,
        &emissiveTexturePath,
        &clearcoatFactor,
        &clearcoatRoughnessFactor,
        &sheenColor,
        &sheenRoughnessFactor,
        &transmissionFactor,
        &thicknessFactor,
        &attenuationColor,
        &attenuationDistance,
        &subsurfaceColor,
        &subsurfaceFactor,
        &anisotropyStrength,
        &anisotropyRotation,
        &layerWeight,
        &decalBlendMode,
        &layerBlendMode,
        &clearcoatTextureAssetId,
        &clearcoatRoughnessTextureAssetId,
        &sheenColorTextureAssetId,
        &transmissionTextureAssetId,
        &thicknessTextureAssetId,
        &anisotropyTextureAssetId,
        &decalTextureAssetId,
        &layerMaskTextureAssetId,
        &clearcoatTexturePath,
        &clearcoatRoughnessTexturePath,
        &sheenColorTexturePath,
        &transmissionTexturePath,
        &thicknessTexturePath,
        &anisotropyTexturePath,
        &decalTexturePath,
        &layerMaskTexturePath,
    };
    return writers;
}

} // namespace

bool RenderMaterialAssetWriter::Save(const std::filesystem::path& path, const RenderMaterialAssetData& asset) {
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    // Atomic save: write to a temp file, flush, then rename.
    const std::filesystem::path tmpPath = path.string() + ".tmp";
    {
        std::ofstream output{ tmpPath, std::ios::trunc | std::ios::binary };
        if (!output) {
            return false;
        }
        Write(output, asset);
        output.flush();
        if (!output) {
            return false;
        }
    }

    std::filesystem::rename(tmpPath, path, error);
    if (error) {
        // Fallback: copy then remove tmp on Windows rename-across-volumes issues
        error.clear();
        std::filesystem::copy_file(tmpPath, path, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return false;
        }
        std::filesystem::remove(tmpPath, error);
    }
    return !error;
}

void RenderMaterialAssetWriter::Write(std::ostream& output, const RenderMaterialAssetData& asset) {
    output << "# KB material\n";
    output << "version " << (asset.documentVersion == 0U ? kRenderMaterialAssetDocumentVersion : asset.documentVersion) << '\n';
    output << "materialType " << (asset.materialType.empty() ? kRenderMaterialAssetBuiltInPbrType : asset.materialType) << '\n';
    output << "materialTypeVersion " << (asset.materialTypeVersion == 0U ? kRenderMaterialAssetBuiltInPbrTypeVersion : asset.materialTypeVersion) << '\n';
    if (asset.materialTypeAssetId != 0U) {
        output << "materialTypeAssetId " << asset.materialTypeAssetId << '\n';
    }
    if (!asset.materialTypeAssetPath.empty()) {
        output << "materialTypeAsset " << asset.materialTypeAssetPath << '\n';
    }
    if (asset.graphSourceAssetId != 0U) {
        output << "graphSourceAssetId " << asset.graphSourceAssetId << '\n';
    }
    if (!asset.graphSourceAssetPath.empty()) {
        output << "graphSourceAsset " << asset.graphSourceAssetPath << '\n';
    }
    for (const IRenderMaterialAssetPropertyWriter* writer : PropertyWriters()) {
        writer->Write(output, asset);
    }
    WriteGraphParameterValues(output, asset);
    WriteGraph(output, asset.graph);
}

} // namespace kb::render
