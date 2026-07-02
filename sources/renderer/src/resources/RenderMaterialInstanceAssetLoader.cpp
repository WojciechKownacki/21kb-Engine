#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "resources/RenderMaterialAssetParser.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace kb::render {
namespace {

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    for (const kb::assets::AssetId existing : dependencies) {
        if (existing == id) {
            return;
        }
    }
    dependencies.push_back(id);
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index];
        char right = rhs[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> ParseUInt64(std::string_view text) noexcept {
    text = Trim(text);
    std::uint64_t value = 0U;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<float> ParseFloat(std::string_view text) noexcept {
    text = Trim(text);
    float value = 0.0F;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<bool> ParseBool(std::string_view text) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "true") || text == "1") {
        return true;
    }
    if (EqualsIgnoreCase(text, "false") || text == "0") {
        return false;
    }
    return std::nullopt;
}

void AddDiagnostic(
    RenderMaterialInstanceAssetParseResult& result,
    RenderMaterialInstanceAssetParseDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text = {}) {
    result.diagnostics.push_back(RenderMaterialInstanceAssetParseDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] bool HasMaterialParseError(const RenderMaterialAssetParseResult& result) noexcept {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ParseGraphBlendModeValue(std::string_view text, RenderMaterialGraphBlendMode& output) noexcept {
    text = Trim(text);
    const RenderMaterialGraphBlendMode parsed = ParseRenderMaterialGraphBlendMode(text);
    if (EqualsIgnoreCase(text, "opaque") ||
        EqualsIgnoreCase(text, "masked") ||
        EqualsIgnoreCase(text, "mask") ||
        EqualsIgnoreCase(text, "translucent") ||
        EqualsIgnoreCase(text, "transparent") ||
        EqualsIgnoreCase(text, "alpha") ||
        EqualsIgnoreCase(text, "additive") ||
        EqualsIgnoreCase(text, "modulate") ||
        EqualsIgnoreCase(text, "alphaComposite") ||
        EqualsIgnoreCase(text, "alpha_composite") ||
        EqualsIgnoreCase(text, "alphacomposite") ||
        EqualsIgnoreCase(text, "premultipliedAlpha") ||
        EqualsIgnoreCase(text, "premultiplied_alpha") ||
        EqualsIgnoreCase(text, "alphaHoldout") ||
        EqualsIgnoreCase(text, "alpha_holdout") ||
        EqualsIgnoreCase(text, "alphaholdout")) {
        output = parsed;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseShadingModelValue(std::string_view text, RenderMaterialShadingModel& output) noexcept {
    text = Trim(text);
    const RenderMaterialShadingModel parsed = ParseRenderMaterialShadingModel(text);
    if (EqualsIgnoreCase(text, "unlit") ||
        EqualsIgnoreCase(text, "lit") ||
        EqualsIgnoreCase(text, "defaultLit") ||
        EqualsIgnoreCase(text, "default_lit") ||
        EqualsIgnoreCase(text, "defaultlit") ||
        EqualsIgnoreCase(text, "subsurface") ||
        EqualsIgnoreCase(text, "clearCoat") ||
        EqualsIgnoreCase(text, "clear_coat") ||
        EqualsIgnoreCase(text, "clearcoat") ||
        EqualsIgnoreCase(text, "cloth") ||
        EqualsIgnoreCase(text, "hair") ||
        EqualsIgnoreCase(text, "eye") ||
        EqualsIgnoreCase(text, "singleLayerWater") ||
        EqualsIgnoreCase(text, "single_layer_water") ||
        EqualsIgnoreCase(text, "singlelayerwater") ||
        EqualsIgnoreCase(text, "thinTranslucent") ||
        EqualsIgnoreCase(text, "thin_translucent") ||
        EqualsIgnoreCase(text, "thintranslucent")) {
        output = parsed;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseDomainValue(std::string_view text, RenderMaterialDomain& output) noexcept {
    text = Trim(text);
    const RenderMaterialDomain parsed = ParseRenderMaterialDomain(text);
    if (EqualsIgnoreCase(text, "surface") ||
        EqualsIgnoreCase(text, "deferredDecal") ||
        EqualsIgnoreCase(text, "deferred_decal") ||
        EqualsIgnoreCase(text, "deferreddecal") ||
        EqualsIgnoreCase(text, "lightFunction") ||
        EqualsIgnoreCase(text, "light_function") ||
        EqualsIgnoreCase(text, "lightfunction") ||
        EqualsIgnoreCase(text, "volume") ||
        EqualsIgnoreCase(text, "postProcess") ||
        EqualsIgnoreCase(text, "post_process") ||
        EqualsIgnoreCase(text, "postprocess") ||
        EqualsIgnoreCase(text, "userInterface") ||
        EqualsIgnoreCase(text, "user_interface") ||
        EqualsIgnoreCase(text, "userinterface") ||
        EqualsIgnoreCase(text, "ui")) {
        output = parsed;
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<RenderMaterialParameterType> ParameterTypeForGraphNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return RenderMaterialParameterType::Scalar;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return RenderMaterialParameterType::Vec4;
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialParameterType::Color;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureSample:
        return RenderMaterialParameterType::Texture;
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::BreakVector:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string StableParameterIdForGraphNode(const RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "scalar" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "vector" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "color" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "texture" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample:
        return "textureSample" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return "staticBool" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return "staticSwitch" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return "staticComponentMask" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::BreakVector:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
        break;
    }
    return "parameter" + std::to_string(node.id);
}

[[nodiscard]] bool IsStaticOverrideNodeKind(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::StaticBoolParameter ||
        kind == RenderMaterialGraphNodeKind::StaticSwitch ||
        kind == RenderMaterialGraphNodeKind::StaticComponentMask;
}

[[nodiscard]] bool IsStaticBoolText(std::string_view text) noexcept {
    return ParseBool(text).has_value();
}

[[nodiscard]] bool IsStaticMaskText(std::string_view text) noexcept {
    text = Trim(text);
    if (text.empty() || text.size() > 4U) {
        return false;
    }
    bool seenR = false;
    bool seenG = false;
    bool seenB = false;
    bool seenA = false;
    for (char channel : text) {
        if (channel >= 'A' && channel <= 'Z') {
            channel = static_cast<char>(channel - 'A' + 'a');
        }
        bool* seen = nullptr;
        switch (channel) {
        case 'r':
        case 'x':
            seen = &seenR;
            break;
        case 'g':
        case 'y':
            seen = &seenG;
            break;
        case 'b':
        case 'z':
            seen = &seenB;
            break;
        case 'a':
        case 'w':
            seen = &seenA;
            break;
        default:
            return false;
        }
        if (*seen) {
            return false;
        }
        *seen = true;
    }
    return true;
}

[[nodiscard]] bool IsStaticOverrideValueValid(RenderMaterialGraphNodeKind kind, std::string_view value) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return IsStaticBoolText(value);
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return IsStaticMaskText(value);
    default:
        return false;
    }
}

[[nodiscard]] const RenderMaterialGraphNode* FindStaticOverrideNode(
    const RenderMaterialGraphDocument& graph,
    std::string_view stableId) noexcept {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (IsStaticOverrideNodeKind(node.kind) && StableParameterIdForGraphNode(node) == stableId) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<RenderMaterialParameterType> FindParentGraphParameterType(
    const RenderMaterialAssetData& parentMaterial,
    std::string_view stableId) {
    for (const RenderMaterialGraphParameterValue& value : parentMaterial.graphParameterValues) {
        if (value.stableId == stableId) {
            return value.type;
        }
    }
    for (const RenderMaterialGraphNode& node : parentMaterial.graph.nodes) {
        if (StableParameterIdForGraphNode(node) != stableId) {
            continue;
        }
        if (const std::optional<RenderMaterialParameterType> type = ParameterTypeForGraphNode(node.kind);
            type.has_value()) {
            return type;
        }
    }
    return std::nullopt;
}

void AppendOverrideDiagnostics(
    RenderMaterialInstanceAssetParseResult& result,
    const RenderMaterialAssetParseResult& materialResult) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : materialResult.diagnostics) {
        if (diagnostic.severity != RenderMaterialAssetParseDiagnosticSeverity::Error) {
            continue;
        }
        std::string message{ "Invalid material instance override: " };
        message += std::string{ RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
        if (!diagnostic.message.empty()) {
            message += ": ";
            message += diagnostic.message;
        }
        AddDiagnostic(
            result,
            RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial,
            diagnostic.line,
            diagnostic.field,
            std::move(message),
            diagnostic.text);
    }
}

void ApplyTextureAssetIdOverrides(RenderMaterialDesc& material, const RenderMaterialDesc& overrides) noexcept {
    if (overrides.albedoTextureAssetId != 0U) material.albedoTextureAssetId = overrides.albedoTextureAssetId;
    if (overrides.normalTextureAssetId != 0U) material.normalTextureAssetId = overrides.normalTextureAssetId;
    if (overrides.metallicRoughnessTextureAssetId != 0U) material.metallicRoughnessTextureAssetId = overrides.metallicRoughnessTextureAssetId;
    if (overrides.occlusionTextureAssetId != 0U) material.occlusionTextureAssetId = overrides.occlusionTextureAssetId;
    if (overrides.emissiveTextureAssetId != 0U) material.emissiveTextureAssetId = overrides.emissiveTextureAssetId;
    if (overrides.clearcoatTextureAssetId != 0U) material.clearcoatTextureAssetId = overrides.clearcoatTextureAssetId;
    if (overrides.clearcoatRoughnessTextureAssetId != 0U) material.clearcoatRoughnessTextureAssetId = overrides.clearcoatRoughnessTextureAssetId;
    if (overrides.sheenColorTextureAssetId != 0U) material.sheenColorTextureAssetId = overrides.sheenColorTextureAssetId;
    if (overrides.transmissionTextureAssetId != 0U) material.transmissionTextureAssetId = overrides.transmissionTextureAssetId;
    if (overrides.thicknessTextureAssetId != 0U) material.thicknessTextureAssetId = overrides.thicknessTextureAssetId;
    if (overrides.anisotropyTextureAssetId != 0U) material.anisotropyTextureAssetId = overrides.anisotropyTextureAssetId;
    if (overrides.decalTextureAssetId != 0U) material.decalTextureAssetId = overrides.decalTextureAssetId;
    if (overrides.layerMaskTextureAssetId != 0U) material.layerMaskTextureAssetId = overrides.layerMaskTextureAssetId;
}

void ApplyTexturePathOverrides(RenderMaterialAssetData& material, const RenderMaterialAssetData& overrides) {
    if (!overrides.albedoTexturePath.empty()) material.albedoTexturePath = overrides.albedoTexturePath;
    if (!overrides.normalTexturePath.empty()) material.normalTexturePath = overrides.normalTexturePath;
    if (!overrides.metallicRoughnessTexturePath.empty()) material.metallicRoughnessTexturePath = overrides.metallicRoughnessTexturePath;
    if (!overrides.occlusionTexturePath.empty()) material.occlusionTexturePath = overrides.occlusionTexturePath;
    if (!overrides.emissiveTexturePath.empty()) material.emissiveTexturePath = overrides.emissiveTexturePath;
    if (!overrides.clearcoatTexturePath.empty()) material.clearcoatTexturePath = overrides.clearcoatTexturePath;
    if (!overrides.clearcoatRoughnessTexturePath.empty()) material.clearcoatRoughnessTexturePath = overrides.clearcoatRoughnessTexturePath;
    if (!overrides.sheenColorTexturePath.empty()) material.sheenColorTexturePath = overrides.sheenColorTexturePath;
    if (!overrides.transmissionTexturePath.empty()) material.transmissionTexturePath = overrides.transmissionTexturePath;
    if (!overrides.thicknessTexturePath.empty()) material.thicknessTexturePath = overrides.thicknessTexturePath;
    if (!overrides.anisotropyTexturePath.empty()) material.anisotropyTexturePath = overrides.anisotropyTexturePath;
    if (!overrides.decalTexturePath.empty()) material.decalTexturePath = overrides.decalTexturePath;
    if (!overrides.layerMaskTexturePath.empty()) material.layerMaskTexturePath = overrides.layerMaskTexturePath;
}

void MergeGraphParameterValues(
    std::vector<RenderMaterialGraphParameterValue>& materialValues,
    const std::vector<RenderMaterialGraphParameterValue>& parentValues) {
    if (materialValues.empty()) {
        materialValues = parentValues;
        return;
    }

    std::vector<RenderMaterialGraphParameterValue> merged = parentValues;
    for (const RenderMaterialGraphParameterValue& overrideValue : materialValues) {
        const auto existing = std::find_if(merged.begin(), merged.end(), [&overrideValue](const RenderMaterialGraphParameterValue& value) {
            return value.stableId == overrideValue.stableId;
        });
        if (existing != merged.end()) {
            *existing = overrideValue;
        } else {
            merged.push_back(overrideValue);
        }
    }
    materialValues = std::move(merged);
}

template <std::size_t Count>
[[nodiscard]] bool FloatArrayDiffers(const float (&lhs)[Count], const float (&rhs)[Count]) noexcept {
    for (std::size_t index = 0U; index < Count; ++index) {
        if (lhs[index] != rhs[index]) {
            return true;
        }
    }
    return false;
}

template <std::size_t Count>
void CopyFloatArray(float (&dst)[Count], const float (&src)[Count]) noexcept {
    for (std::size_t index = 0U; index < Count; ++index) {
        dst[index] = src[index];
    }
}

void ApplyLegacyDescOverrides(RenderMaterialDesc& material, const RenderMaterialDesc& overrides) noexcept {
    const RenderMaterialDesc defaults{};
    if (FloatArrayDiffers(overrides.baseColor, defaults.baseColor)) CopyFloatArray(material.baseColor, overrides.baseColor);
    if (FloatArrayDiffers(overrides.emissiveColor, defaults.emissiveColor)) CopyFloatArray(material.emissiveColor, overrides.emissiveColor);
    if (overrides.metallicFactor != defaults.metallicFactor) material.metallicFactor = overrides.metallicFactor;
    if (overrides.roughnessFactor != defaults.roughnessFactor) material.roughnessFactor = overrides.roughnessFactor;
    if (overrides.normalScale != defaults.normalScale) material.normalScale = overrides.normalScale;
    if (overrides.occlusionStrength != defaults.occlusionStrength) material.occlusionStrength = overrides.occlusionStrength;
    if (overrides.emissiveStrength != defaults.emissiveStrength) material.emissiveStrength = overrides.emissiveStrength;
    if (overrides.alphaCutoff != defaults.alphaCutoff) material.alphaCutoff = overrides.alphaCutoff;
    if (FloatArrayDiffers(overrides.uvTiling, defaults.uvTiling)) CopyFloatArray(material.uvTiling, overrides.uvTiling);
    if (FloatArrayDiffers(overrides.uvOffset, defaults.uvOffset)) CopyFloatArray(material.uvOffset, overrides.uvOffset);
    if (overrides.clearcoatFactor != defaults.clearcoatFactor) material.clearcoatFactor = overrides.clearcoatFactor;
    if (overrides.clearcoatRoughnessFactor != defaults.clearcoatRoughnessFactor) material.clearcoatRoughnessFactor = overrides.clearcoatRoughnessFactor;
    if (FloatArrayDiffers(overrides.sheenColor, defaults.sheenColor)) CopyFloatArray(material.sheenColor, overrides.sheenColor);
    if (overrides.sheenRoughnessFactor != defaults.sheenRoughnessFactor) material.sheenRoughnessFactor = overrides.sheenRoughnessFactor;
    if (overrides.transmissionFactor != defaults.transmissionFactor) material.transmissionFactor = overrides.transmissionFactor;
    if (overrides.thicknessFactor != defaults.thicknessFactor) material.thicknessFactor = overrides.thicknessFactor;
    if (FloatArrayDiffers(overrides.attenuationColor, defaults.attenuationColor)) CopyFloatArray(material.attenuationColor, overrides.attenuationColor);
    if (overrides.attenuationDistance != defaults.attenuationDistance) material.attenuationDistance = overrides.attenuationDistance;
    if (FloatArrayDiffers(overrides.subsurfaceColor, defaults.subsurfaceColor)) CopyFloatArray(material.subsurfaceColor, overrides.subsurfaceColor);
    if (overrides.subsurfaceFactor != defaults.subsurfaceFactor) material.subsurfaceFactor = overrides.subsurfaceFactor;
    if (overrides.anisotropyStrength != defaults.anisotropyStrength) material.anisotropyStrength = overrides.anisotropyStrength;
    if (overrides.anisotropyRotation != defaults.anisotropyRotation) material.anisotropyRotation = overrides.anisotropyRotation;
    if (overrides.layerWeight != defaults.layerWeight) material.layerWeight = overrides.layerWeight;
    if (overrides.alphaMode != defaults.alphaMode) material.alphaMode = overrides.alphaMode;
    if (overrides.decalBlendMode != defaults.decalBlendMode) material.decalBlendMode = overrides.decalBlendMode;
    if (overrides.layerBlendMode != defaults.layerBlendMode) material.layerBlendMode = overrides.layerBlendMode;
    if (overrides.translucencyBlend != defaults.translucencyBlend) material.translucencyBlend = overrides.translucencyBlend;
    if (overrides.doubleSided != defaults.doubleSided) material.doubleSided = overrides.doubleSided;
    if (overrides.writesDepth != defaults.writesDepth) material.writesDepth = overrides.writesDepth;
    ApplyTextureAssetIdOverrides(material, overrides);
}

[[nodiscard]] std::string FloatToText(float value) {
    std::ostringstream output;
    output << std::setprecision(9) << value;
    return output.str();
}

void ApplyGraphBlendModeToDesc(RenderMaterialDesc& desc, RenderMaterialGraphBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialGraphBlendMode::Opaque:
        desc.alphaMode = RenderMaterialAlphaMode::Opaque;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::Alpha;
        break;
    case RenderMaterialGraphBlendMode::Masked:
        desc.alphaMode = RenderMaterialAlphaMode::Mask;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::Alpha;
        break;
    case RenderMaterialGraphBlendMode::Translucent:
        desc.alphaMode = RenderMaterialAlphaMode::Blend;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::Alpha;
        break;
    case RenderMaterialGraphBlendMode::Additive:
        desc.alphaMode = RenderMaterialAlphaMode::Blend;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::Additive;
        break;
    case RenderMaterialGraphBlendMode::Modulate:
        desc.alphaMode = RenderMaterialAlphaMode::Blend;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::Modulate;
        break;
    case RenderMaterialGraphBlendMode::AlphaComposite:
        desc.alphaMode = RenderMaterialAlphaMode::Blend;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::PreMultipliedAlpha;
        break;
    case RenderMaterialGraphBlendMode::AlphaHoldout:
        desc.alphaMode = RenderMaterialAlphaMode::Blend;
        desc.translucencyBlend = RenderMaterialTranslucencyBlend::AlphaHoldout;
        break;
    }
}

[[nodiscard]] bool GraphHasInputLink(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == nodeId && link.toPin == pin) {
            return true;
        }
    }
    return false;
}

void ApplyOpacityMaskClipGraphDefault(RenderMaterialGraphDocument& graph, float opacityMaskClip) {
    if (!HasGraphAuthoringData(graph)) {
        return;
    }
    RenderMaterialGraphNode* outputNode = nullptr;
    std::uint32_t maxNodeId = 0U;
    for (RenderMaterialGraphNode& node : graph.nodes) {
        maxNodeId = std::max(maxNodeId, node.id);
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput && outputNode == nullptr) {
            outputNode = &node;
        }
    }
    if (outputNode == nullptr || GraphHasInputLink(graph, outputNode->id, "alphaClipThreshold")) {
        return;
    }

    RenderMaterialGraphNode threshold{
        .id = maxNodeId + 1U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = outputNode->positionX - 240,
        .positionY = outputNode->positionY + 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = FloatToText(opacityMaskClip) },
    };
    RenderMaterialGraphLink link{
        .fromNodeId = threshold.id,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantScalar, "value", true),
        .fromPin = "value",
        .toNodeId = outputNode->id,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "alphaClipThreshold", false),
        .toPin = "alphaClipThreshold",
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    graph.nodes.push_back(std::move(threshold));
    graph.links.push_back(std::move(link));
}

void ApplyStaticParameterOverrides(
    RenderMaterialGraphDocument& graph,
    const std::vector<RenderMaterialInstanceStaticParameterOverride>& overrides) {
    if (overrides.empty() || !HasGraphAuthoringData(graph)) {
        return;
    }
    for (const RenderMaterialInstanceStaticParameterOverride& overrideValue : overrides) {
        for (RenderMaterialGraphNode& node : graph.nodes) {
            if (node.kind == overrideValue.nodeKind && StableParameterIdForGraphNode(node) == overrideValue.stableId) {
                node.parameter.defaultValueHint = overrideValue.value;
                break;
            }
        }
    }
}

void ApplyBasePropertyOverrides(
    RenderMaterialAssetData& material,
    const RenderMaterialInstanceBasePropertyOverrides& overrides) {
    if (!overrides.HasAny()) {
        return;
    }
    if (overrides.overrideBlendMode) {
        material.graph.blendMode = std::string{ RenderMaterialGraphBlendModeName(overrides.blendMode) };
        ApplyGraphBlendModeToDesc(material.desc, overrides.blendMode);
    }
    if (overrides.overrideShadingModel) {
        material.graph.shadingModel = std::string{ RenderMaterialShadingModelName(overrides.shadingModel) };
    }
    if (overrides.overrideTwoSided) {
        material.desc.doubleSided = overrides.twoSided;
    }
    if (overrides.overrideOpacityMaskClip) {
        material.desc.alphaCutoff = std::clamp(overrides.opacityMaskClip, 0.0F, 1.0F);
        ApplyOpacityMaskClipGraphDefault(material.graph, material.desc.alphaCutoff);
    }
    if (overrides.overrideDomain) {
        material.graph.materialDomain = std::string{ RenderMaterialDomainName(overrides.domain) };
    }
}

[[nodiscard]] RenderMaterialInstanceAssetParseResult Parse(std::istream& input) {
    RenderMaterialInstanceAssetParseResult result;
    RenderMaterialInstanceAssetData asset{};
    bool sawContent = false;
    bool sawOverrideContent = false;
    std::ostringstream overrideDocument;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view text = Trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        sawContent = true;

        const std::size_t split = text.find_first_of(" \t");
        const std::string_view field = split == std::string_view::npos ? text : text.substr(0U, split);
        const std::string_view value = split == std::string_view::npos ? std::string_view{} : Trim(text.substr(split + 1U));
        if (field == "version") {
            const std::optional<std::uint64_t> version = ParseUInt64(value);
            if (!version.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidDocumentVersion, lineNumber, "version", "Material instance version must be an unsigned integer.", std::string{ value });
                continue;
            }
            if (*version != kRenderMaterialInstanceAssetDocumentVersion) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::UnsupportedDocumentVersion, lineNumber, "version", "Material instance document version is not supported.", std::string{ value });
                continue;
            }
            asset.documentVersion = static_cast<std::uint32_t>(*version);
            asset.hasExplicitDocumentVersion = true;
        } else if (field == "parentMaterialAssetId") {
            const std::optional<std::uint64_t> id = ParseUInt64(value);
            if (!id.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "parentMaterialAssetId", "Parent material asset id must be an unsigned integer.", std::string{ value });
                continue;
            }
            asset.parentMaterialAssetId = kb::assets::AssetId{ *id };
        } else if (field == "staticOverride") {
            std::istringstream parser{ std::string{ value } };
            std::string stableId;
            std::string kindText;
            std::string staticValue;
            parser >> stableId >> kindText >> staticValue;
            if (stableId.empty() || kindText.empty() || staticValue.empty()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "staticOverride", "Static override must be '<stableId> <nodeKind> <value>'.", std::string{ value });
                continue;
            }
            std::string trailing;
            parser >> trailing;
            if (!trailing.empty()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "staticOverride", "Static override value must be a single token.", std::string{ value });
                continue;
            }
            const std::optional<RenderMaterialGraphNodeKind> kind = ParseRenderMaterialGraphNodeKind(kindText);
            if (!kind.has_value() || !IsStaticOverrideNodeKind(*kind)) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "staticOverride", "Static override node kind must be StaticBoolParameter, StaticSwitch, or StaticComponentMask.", std::string{ kindText });
                continue;
            }
            if (!IsStaticOverrideValueValid(*kind, staticValue)) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "staticOverride", "Static override value is not valid for the node kind.", std::string{ staticValue });
                continue;
            }
            asset.staticParameterOverrides.push_back(RenderMaterialInstanceStaticParameterOverride{
                .stableId = std::move(stableId),
                .nodeKind = *kind,
                .value = std::move(staticValue),
            });
        } else if (field == "bOverride_blendMode") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "bOverride_blendMode", "Blend-mode override flag must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.overrideBlendMode = *parsed;
        } else if (field == "blendMode") {
            RenderMaterialGraphBlendMode mode = RenderMaterialGraphBlendMode::Opaque;
            if (!ParseGraphBlendModeValue(value, mode)) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "blendMode", "Material instance blend mode is not supported.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.blendMode = mode;
        } else if (field == "bOverride_shadingModel") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "bOverride_shadingModel", "Shading-model override flag must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.overrideShadingModel = *parsed;
        } else if (field == "shadingModel") {
            RenderMaterialShadingModel model = RenderMaterialShadingModel::DefaultLit;
            if (!ParseShadingModelValue(value, model)) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "shadingModel", "Material instance shading model is not supported.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.shadingModel = model;
        } else if (field == "bOverride_twoSided") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "bOverride_twoSided", "Two-sided override flag must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.overrideTwoSided = *parsed;
        } else if (field == "twoSided") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "twoSided", "Two-sided value must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.twoSided = *parsed;
        } else if (field == "bOverride_opacityMaskClip") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "bOverride_opacityMaskClip", "Opacity-mask clip override flag must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.overrideOpacityMaskClip = *parsed;
        } else if (field == "opacityMaskClip") {
            const std::optional<float> parsed = ParseFloat(value);
            if (!parsed.has_value() || *parsed < 0.0F || *parsed > 1.0F) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "opacityMaskClip", "Opacity-mask clip value must be in [0, 1].", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.opacityMaskClip = *parsed;
        } else if (field == "bOverride_domain") {
            const std::optional<bool> parsed = ParseBool(value);
            if (!parsed.has_value()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "bOverride_domain", "Domain override flag must be boolean.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.overrideDomain = *parsed;
        } else if (field == "domain") {
            RenderMaterialDomain domain = RenderMaterialDomain::Surface;
            if (!ParseDomainValue(value, domain)) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue, lineNumber, "domain", "Material instance domain is not supported.", std::string{ value });
                continue;
            }
            asset.basePropertyOverrides.domain = domain;
        } else {
            sawOverrideContent = true;
            overrideDocument << text << '\n';
        }
    }

    if (!sawContent) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::EmptyDocument, 0U, {}, "Material instance document is empty.");
    }
    if (!asset.parentMaterialAssetId.IsValid()) {
        AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::MissingParentMaterial, 0U, "parentMaterialAssetId", "Material instance is missing a parent material asset reference.");
    }
    if (sawOverrideContent) {
        std::istringstream overrideInput{ overrideDocument.str() };
        const RenderMaterialAssetParseResult overrides = RenderMaterialAssetParser::ParseWithDiagnostics(overrideInput);
        if (!overrides.asset.has_value() || HasMaterialParseError(overrides)) {
            AppendOverrideDiagnostics(result, overrides);
            if (result.diagnostics.empty()) {
                AddDiagnostic(result, RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial, 0U, {}, "Material instance override document is invalid.");
            }
        } else {
            asset.overrides = *overrides.asset;
            asset.hasOverrides = true;
        }
    }
    if (result.diagnostics.empty()) {
        result.asset = std::move(asset);
    }
    return result;
}

} // namespace

std::string_view RenderMaterialInstanceAssetParseDiagnosticCodeName(RenderMaterialInstanceAssetParseDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialInstanceAssetParseDiagnosticCode::FileOpenFailed:
        return "file_open_failed";
    case RenderMaterialInstanceAssetParseDiagnosticCode::EmptyDocument:
        return "empty_document";
    case RenderMaterialInstanceAssetParseDiagnosticCode::UnknownField:
        return "unknown_field";
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidFieldValue:
        return "invalid_field_value";
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidDocumentVersion:
        return "invalid_document_version";
    case RenderMaterialInstanceAssetParseDiagnosticCode::UnsupportedDocumentVersion:
        return "unsupported_document_version";
    case RenderMaterialInstanceAssetParseDiagnosticCode::MissingParentMaterial:
        return "missing_parent_material";
    case RenderMaterialInstanceAssetParseDiagnosticCode::InvalidOverrideMaterial:
        return "invalid_override_material";
    }
    return "unknown_diagnostic";
}

std::string_view RenderMaterialInstanceValidationDiagnosticCodeName(RenderMaterialInstanceValidationDiagnosticCode code) noexcept {
    switch (code) {
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType:
        return "incompatible_material_type";
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialTypeVersion:
        return "incompatible_material_type_version";
    case RenderMaterialInstanceValidationDiagnosticCode::UnknownOverrideParameter:
        return "unknown_override_parameter";
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleOverrideParameterType:
        return "incompatible_override_parameter_type";
    case RenderMaterialInstanceValidationDiagnosticCode::UnknownStaticOverrideParameter:
        return "unknown_static_override_parameter";
    case RenderMaterialInstanceValidationDiagnosticCode::IncompatibleStaticOverrideParameterType:
        return "incompatible_static_override_parameter_type";
    case RenderMaterialInstanceValidationDiagnosticCode::InvalidStaticOverrideValue:
        return "invalid_static_override_value";
    }
    return "unknown_diagnostic";
}

RenderMaterialAssetData BuildEffectiveRenderMaterialInstanceAsset(
    const RenderMaterialAssetData& parentMaterial,
    const RenderMaterialInstanceAssetData& instance) {
    if (!instance.hasOverrides &&
        instance.staticParameterOverrides.empty() &&
        !instance.basePropertyOverrides.HasAny()) {
        return parentMaterial;
    }

    RenderMaterialAssetData material = parentMaterial;
    if (instance.hasOverrides) {
        if (instance.overrides.materialTypeAssetId != 0U) {
            material.materialTypeAssetId = instance.overrides.materialTypeAssetId;
        }
        if (!instance.overrides.materialTypeAssetPath.empty()) {
            material.materialTypeAssetPath = instance.overrides.materialTypeAssetPath;
        }
        if (instance.overrides.graphSourceAssetId != 0U) {
            material.graphSourceAssetId = instance.overrides.graphSourceAssetId;
        }
        if (!instance.overrides.graphSourceAssetPath.empty()) {
            material.graphSourceAssetPath = instance.overrides.graphSourceAssetPath;
        }
        ApplyLegacyDescOverrides(material.desc, instance.overrides.desc);
        ApplyTexturePathOverrides(material, instance.overrides);
        if (HasGraphAuthoringData(instance.overrides.graph)) {
            material.graph = instance.overrides.graph;
        }
        std::vector<RenderMaterialGraphParameterValue> mergedParameterValues = instance.overrides.graphParameterValues;
        MergeGraphParameterValues(mergedParameterValues, material.graphParameterValues);
        material.graphParameterValues = std::move(mergedParameterValues);
    }
    ApplyStaticParameterOverrides(material.graph, instance.staticParameterOverrides);
    ApplyBasePropertyOverrides(material, instance.basePropertyOverrides);
    return material;
}

bool RenderMaterialInstanceValidationResult::Succeeded() const noexcept {
    return diagnostics.empty();
}

bool RenderMaterialInstanceAssetParseResult::Succeeded() const noexcept {
    return asset.has_value() && diagnostics.empty();
}

std::string RenderMaterialInstanceAssetParseResult::ErrorMessage() const {
    if (diagnostics.empty()) {
        return {};
    }
    std::ostringstream output;
    output << "Render material instance asset load failed";
    for (const RenderMaterialInstanceAssetParseDiagnostic& diagnostic : diagnostics) {
        output << "; code " << RenderMaterialInstanceAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.assetId.IsValid()) {
            output << ", asset " << diagnostic.assetId.value;
        }
        if (!diagnostic.path.empty()) {
            output << ", path " << diagnostic.path.generic_string();
        }
        output << ": ";
        if (diagnostic.line > 0U) {
            output << "line " << diagnostic.line << ": ";
        }
        output << diagnostic.message;
        if (!diagnostic.text.empty()) {
            output << " [" << diagnostic.text << "]";
        }
    }
    return output.str();
}

std::string_view RenderMaterialInstanceAssetLoader::Type() const noexcept {
    return "RenderMaterialInstance";
}

std::type_index RenderMaterialInstanceAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialInstanceAssetData);
}

std::vector<std::string> RenderMaterialInstanceAssetLoader::Extensions() const {
    return { ".kbmatinst" };
}

kb::assets::AssetLoadResult RenderMaterialInstanceAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialInstanceAssetParseResult material = LoadInstanceWithDiagnostics(request.resolvedPath, request.metadata.id);
    if (!material.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = material.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialInstanceAssetData>(*material.asset),
        .error = {},
    };
}

std::vector<kb::assets::AssetId> RenderMaterialInstanceAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const RenderMaterialInstanceAssetParseResult instance = LoadInstanceWithDiagnostics(metadata.physicalPath, metadata.id);
    if (!instance.asset.has_value()) {
        return {};
    }

    std::vector<kb::assets::AssetId> dependencies;
    dependencies.reserve(16U);
    AppendUnique(dependencies, instance.asset->parentMaterialAssetId);
    if (instance.asset->hasOverrides) {
        std::vector<kb::assets::AssetId> overrideDependencies =
            RenderMaterialAssetLoader::DiscoverMaterialDependencies(instance.asset->overrides, metadata, registry);
        for (const kb::assets::AssetId dependency : overrideDependencies) {
            AppendUnique(dependencies, dependency);
        }
    }
    return dependencies;
}

std::optional<RenderMaterialInstanceAssetData> RenderMaterialInstanceAssetLoader::LoadInstance(const std::filesystem::path& path) {
    RenderMaterialInstanceAssetParseResult result = LoadInstanceWithDiagnostics(path);
    return result.asset;
}

std::optional<RenderMaterialInstanceAssetData> RenderMaterialInstanceAssetLoader::LoadInstance(std::istream& input) {
    RenderMaterialInstanceAssetParseResult result = LoadInstanceWithDiagnostics(input);
    return result.asset;
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(const std::filesystem::path& path) {
    return LoadInstanceWithDiagnostics(path, {});
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        RenderMaterialInstanceAssetParseResult result;
        result.diagnostics.push_back(RenderMaterialInstanceAssetParseDiagnostic{
            .code = RenderMaterialInstanceAssetParseDiagnosticCode::FileOpenFailed,
            .assetId = assetId,
            .path = path,
            .message = "Material instance file could not be opened.",
        });
        return result;
    }
    RenderMaterialInstanceAssetParseResult result = Parse(input);
    for (RenderMaterialInstanceAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.assetId = assetId;
        diagnostic.path = path;
    }
    return result;
}

RenderMaterialInstanceAssetParseResult RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(std::istream& input) {
    return Parse(input);
}

RenderMaterialInstanceValidationResult RenderMaterialInstanceAssetLoader::ValidateAgainstParent(
    const RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial) {
    RenderMaterialInstanceValidationResult result{};
    if (instance.hasOverrides) {
        if (instance.overrides.materialType != parentMaterial.materialType) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType,
                .message = "Material instance override type '" + instance.overrides.materialType + "' does not match parent material type '" + parentMaterial.materialType + "'.",
            });
        }
        if (instance.overrides.materialTypeVersion != parentMaterial.materialTypeVersion) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialTypeVersion,
                .message = "Material instance override type version " + std::to_string(instance.overrides.materialTypeVersion) +
                    " does not match parent material type version " + std::to_string(parentMaterial.materialTypeVersion) + ".",
            });
        }
        for (const RenderMaterialGraphParameterValue& overrideValue : instance.overrides.graphParameterValues) {
            const std::optional<RenderMaterialParameterType> parentType =
                FindParentGraphParameterType(parentMaterial, overrideValue.stableId);
            if (!parentType.has_value()) {
                result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                    .code = RenderMaterialInstanceValidationDiagnosticCode::UnknownOverrideParameter,
                    .message = "Material instance override parameter '" + overrideValue.stableId + "' is not exposed by its parent material.",
                });
                continue;
            }
            if (*parentType != overrideValue.type) {
                result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                    .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleOverrideParameterType,
                    .message = "Material instance override parameter '" + overrideValue.stableId + "' has a type that does not match its parent material parameter.",
                });
            }
        }
    }
    for (const RenderMaterialInstanceStaticParameterOverride& overrideValue : instance.staticParameterOverrides) {
        const RenderMaterialGraphNode* parentNode = FindStaticOverrideNode(parentMaterial.graph, overrideValue.stableId);
        if (parentNode == nullptr) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::UnknownStaticOverrideParameter,
                .message = "Material instance static override '" + overrideValue.stableId + "' is not exposed by its parent material graph.",
            });
            continue;
        }
        if (parentNode->kind != overrideValue.nodeKind) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::IncompatibleStaticOverrideParameterType,
                .message = "Material instance static override '" + overrideValue.stableId + "' targets " +
                    std::string{ RenderMaterialGraphNodeKindName(overrideValue.nodeKind) } +
                    " but the parent graph exposes " + std::string{ RenderMaterialGraphNodeKindName(parentNode->kind) } + ".",
            });
            continue;
        }
        if (!IsStaticOverrideValueValid(overrideValue.nodeKind, overrideValue.value)) {
            result.diagnostics.push_back(RenderMaterialInstanceValidationDiagnostic{
                .code = RenderMaterialInstanceValidationDiagnosticCode::InvalidStaticOverrideValue,
                .message = "Material instance static override '" + overrideValue.stableId + "' has a value that is invalid for " +
                    std::string{ RenderMaterialGraphNodeKindName(overrideValue.nodeKind) } + ".",
            });
        }
    }
    return result;
}

} // namespace kb::render
