#include "rendering/material_graph/MaterialGraphCanvasDocumentAdapter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] MaterialGraphCanvasColor Color(float r, float g, float b) noexcept {
    return MaterialGraphCanvasColor{ r, g, b, 1.0F };
}

[[nodiscard]] std::string NodeStableId(std::uint32_t nodeId) {
    return std::to_string(nodeId);
}

[[nodiscard]] MaterialGraphCanvasPin BuildPin(
    const kb::render::RenderMaterialGraphNode& node,
    const std::string& pin,
    bool output) {
    return MaterialGraphCanvasPin{
        .label = pin,
        .stableId = pin,
        .type = MaterialGraphCanvasPinTypeFromRenderType(kb::render::RenderMaterialGraphPinDataType(node, pin, output)),
    };
}

[[nodiscard]] std::vector<std::string> InputPinsForCanvas(const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
        return {
            "baseColor",
            "normal",
            "roughness",
            "metallic",
            "specular",
            "emissive",
            "occlusion",
            "alpha",
            "alphaClipThreshold",
            "tangentOutput",
            "attributes",
            "worldPositionOffset",
            "customizedUv0",
            "displacement",
        };
    }
    return kb::render::RenderMaterialGraphNodeInputPinNames(node);
}

[[nodiscard]] MaterialGraphCanvasColor HeaderColor(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    using kb::render::RenderMaterialGraphNodeKind;
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return Color(0.46F, 0.26F, 0.26F);
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ColorRamp:
    case RenderMaterialGraphNodeKind::HsvToRgb:
    case RenderMaterialGraphNodeKind::RgbToHsv:
        return Color(0.40F, 0.34F, 0.20F);
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::CollectionParameter:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return Color(0.22F, 0.36F, 0.28F);
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return Color(0.34F, 0.26F, 0.46F);
    case RenderMaterialGraphNodeKind::CustomCode:
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
    case RenderMaterialGraphNodeKind::FunctionInput:
    case RenderMaterialGraphNodeKind::FunctionOutput:
        return Color(0.20F, 0.34F, 0.40F);
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
    case RenderMaterialGraphNodeKind::If:
        return Color(0.22F, 0.30F, 0.44F);
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        return Color(0.28F, 0.28F, 0.30F);
    default:
        return Color(0.22F, 0.30F, 0.44F);
    }
}

[[nodiscard]] bool IsTexturePreviewKind(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    using kb::render::RenderMaterialGraphNodeKind;
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] float WidthOverride(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    using kb::render::RenderMaterialGraphNodeKind;
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return 204.0F;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return 320.0F;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return 300.0F;
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return 160.0F;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return 176.0F;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return 218.0F;
    case RenderMaterialGraphNodeKind::CustomCode:
        return 220.0F;
    case RenderMaterialGraphNodeKind::Reroute:
        return 116.0F;
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        return 180.0F;
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return 220.0F;
    case RenderMaterialGraphNodeKind::Uv:
        return 164.0F;
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return 420.0F;
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return 282.0F;
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return 220.0F;
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
        return 150.0F;
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case RenderMaterialGraphNodeKind::Desaturate:
        return 166.0F;
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::Exponential:
    case RenderMaterialGraphNodeKind::Exponential2:
    case RenderMaterialGraphNodeKind::Logarithm:
    case RenderMaterialGraphNodeKind::Logarithm2:
    case RenderMaterialGraphNodeKind::SrgbToLinear:
    case RenderMaterialGraphNodeKind::LinearToSrgb:
    case RenderMaterialGraphNodeKind::Logarithm10:
    case RenderMaterialGraphNodeKind::HsvToRgb:
    case RenderMaterialGraphNodeKind::RgbToHsv:
    case RenderMaterialGraphNodeKind::DeriveNormalZ:
        return 170.0F;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return 220.0F;
    case RenderMaterialGraphNodeKind::PartialDerivativeX:
    case RenderMaterialGraphNodeKind::PartialDerivativeY:
    case RenderMaterialGraphNodeKind::BlackBody:
    case RenderMaterialGraphNodeKind::Noise:
    case RenderMaterialGraphNodeKind::VectorNoise:
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case RenderMaterialGraphNodeKind::Transform:
    case RenderMaterialGraphNodeKind::TransformPosition:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
        return 142.0F;
    case RenderMaterialGraphNodeKind::ColorRamp:
        return 310.0F;
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::InverseLerp:
    case RenderMaterialGraphNodeKind::Sobol:
        return 168.0F;
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::BreakVector:
        return 180.0F;
    case RenderMaterialGraphNodeKind::Step:
        return 154.0F;
    case RenderMaterialGraphNodeKind::If:
        return 194.0F;
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        return 202.0F;
    default:
        return 240.0F;
    }
}

[[nodiscard]] float HeightOverride(kb::render::RenderMaterialGraphNodeKind kind) noexcept {
    using kb::render::RenderMaterialGraphNodeKind;
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
        return 76.0F;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return 100.0F;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return 196.0F;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return 162.0F;
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return 66.0F;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return 72.0F;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return 176.0F;
    case RenderMaterialGraphNodeKind::CustomCode:
        return 88.0F;
    case RenderMaterialGraphNodeKind::Reroute:
        return 54.0F;
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        return 58.0F;
    case RenderMaterialGraphNodeKind::QualitySwitch:
        return 128.0F;
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return 106.0F;
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return 86.0F;
    case RenderMaterialGraphNodeKind::Uv:
        return 52.0F;
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return 232.0F;
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return 270.0F;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return 230.0F;
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return 96.0F;
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::Exponential:
    case RenderMaterialGraphNodeKind::Exponential2:
    case RenderMaterialGraphNodeKind::Logarithm:
    case RenderMaterialGraphNodeKind::Logarithm2:
    case RenderMaterialGraphNodeKind::SrgbToLinear:
    case RenderMaterialGraphNodeKind::LinearToSrgb:
    case RenderMaterialGraphNodeKind::Logarithm10:
    case RenderMaterialGraphNodeKind::HsvToRgb:
    case RenderMaterialGraphNodeKind::RgbToHsv:
    case RenderMaterialGraphNodeKind::DeriveNormalZ:
        return 62.0F;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return 64.0F;
    case RenderMaterialGraphNodeKind::PartialDerivativeX:
    case RenderMaterialGraphNodeKind::PartialDerivativeY:
    case RenderMaterialGraphNodeKind::BlackBody:
    case RenderMaterialGraphNodeKind::Noise:
    case RenderMaterialGraphNodeKind::VectorNoise:
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case RenderMaterialGraphNodeKind::Transform:
    case RenderMaterialGraphNodeKind::TransformPosition:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
        return 54.0F;
    case RenderMaterialGraphNodeKind::ColorRamp:
        return 150.0F;
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::InverseLerp:
    case RenderMaterialGraphNodeKind::Sobol:
        return 92.0F;
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::BreakVector:
        return 112.0F;
    case RenderMaterialGraphNodeKind::Step:
        return 68.0F;
    case RenderMaterialGraphNodeKind::If:
        return 142.0F;
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        return 174.0F;
    default:
        return 160.0F;
    }
}

[[nodiscard]] std::array<float, 4U> ParseFloat4(std::string_view text, std::array<float, 4U> fallback) noexcept {
    std::array<float, 4U> value = fallback;
    std::size_t index = 0U;
    while (!text.empty() && index < value.size()) {
        const std::size_t start = text.find_first_not_of(" \t\r\n,;");
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        const std::size_t end = text.find_first_of(" \t\r\n,;");
        const std::string_view token = end == std::string_view::npos ? text : text.substr(0U, end);
        float parsed = value[index];
        const char* first = token.data();
        const char* last = token.data() + token.size();
        const std::from_chars_result result = std::from_chars(first, last, parsed);
        if (result.ec == std::errc{} && result.ptr == last) {
            value[index++] = parsed;
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end);
    }
    return value;
}

void AddScalarField(MaterialGraphCanvasNode& canvasNode, const std::string& text) {
    canvasNode.valueFields.push_back(MaterialGraphCanvasValueField{
        .label = {},
        .text = text.empty() ? "0" : text,
        .componentIndex = 0,
    });
}

void AddVectorFields(MaterialGraphCanvasNode& canvasNode, const std::array<float, 4U>& values, std::size_t count) {
    static constexpr std::array<std::string_view, 4U> kLabels{ "X", "Y", "Z", "W" };
    for (std::size_t index = 0U; index < count && index < values.size(); ++index) {
        std::ostringstream text;
        text << values[index];
        canvasNode.valueFields.push_back(MaterialGraphCanvasValueField{
            .label = std::string{ kLabels[index] },
            .text = text.str(),
            .componentIndex = static_cast<int>(index),
        });
    }
}

void AddColorFields(MaterialGraphCanvasNode& canvasNode, const std::array<float, 4U>& values) {
    MaterialGraphCanvasValueField swatch{
        .editable = true,
        .isColorSwatch = true,
        .swatchColor = MaterialGraphCanvasColor{ values[0], values[1], values[2], values[3] },
        .componentIndex = -1,
        .rowSpan = 3,
    };
    canvasNode.valueFields.push_back(std::move(swatch));

    MaterialGraphCanvasValueField rgba{
        .editable = true,
        .rgbaLabels = { "R", "G", "B", "A" },
        .componentIndex = 0,
    };
    for (float value : values) {
        std::ostringstream text;
        text << value;
        rgba.rgbaTexts.push_back(text.str());
    }
    canvasNode.valueFields.push_back(std::move(rgba));
}

void AddValueFields(MaterialGraphCanvasNode& canvasNode, const kb::render::RenderMaterialGraphNode& node) {
    using kb::render::RenderMaterialGraphNodeKind;
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        AddScalarField(canvasNode, node.parameter.defaultValueHint);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        AddVectorFields(canvasNode, ParseFloat4(node.parameter.defaultValueHint, { 0.0F, 0.0F, 0.0F, 0.0F }), 2U);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        AddVectorFields(canvasNode, ParseFloat4(node.parameter.defaultValueHint, { 0.0F, 0.0F, 0.0F, 0.0F }), 3U);
        break;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        AddColorFields(canvasNode, ParseFloat4(node.parameter.defaultValueHint, { 1.0F, 1.0F, 1.0F, 1.0F }));
        break;
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        AddScalarField(canvasNode, node.parameter.defaultValueHint.empty() ? "false" : node.parameter.defaultValueHint);
        break;
    default:
        break;
    }
}

} // namespace

MaterialGraphCanvasPinType MaterialGraphCanvasPinTypeFromRenderType(
    kb::render::RenderMaterialGraphPinType type) noexcept {
    using kb::render::RenderMaterialGraphPinType;
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return MaterialGraphCanvasPinType::Float;
    case RenderMaterialGraphPinType::Float2:
        return MaterialGraphCanvasPinType::Float2;
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return MaterialGraphCanvasPinType::Float3;
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Unknown:
        return MaterialGraphCanvasPinType::Float4;
    case RenderMaterialGraphPinType::Color:
        return MaterialGraphCanvasPinType::Color;
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
        return MaterialGraphCanvasPinType::Texture;
    case RenderMaterialGraphPinType::Bool:
        return MaterialGraphCanvasPinType::Bool;
    case RenderMaterialGraphPinType::MaterialAttributes:
        return MaterialGraphCanvasPinType::MaterialAttributes;
    }
    return MaterialGraphCanvasPinType::Float4;
}

MaterialGraphCanvasNode BuildMaterialGraphCanvasNode(const kb::render::RenderMaterialGraphNode& node) {
    MaterialGraphCanvasNode canvasNode{
        .title = node.parameter.displayName.empty()
            ? std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) }
            : node.parameter.displayName,
        .stableId = NodeStableId(node.id),
        .x = static_cast<float>(node.positionX),
        .y = static_cast<float>(node.positionY),
        .headerColor = HeaderColor(node.kind),
        .isOutput = node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        .widthOverride = WidthOverride(node.kind),
        .heightOverride = HeightOverride(node.kind),
    };

    for (const std::string& pin : InputPinsForCanvas(node)) {
        canvasNode.inputs.push_back(BuildPin(node, pin, false));
    }
    for (const std::string& pin : kb::render::RenderMaterialGraphNodeOutputPinNames(node)) {
        canvasNode.outputs.push_back(BuildPin(node, pin, true));
    }

    canvasNode.texturePreview.enabled = IsTexturePreviewKind(node.kind);
    canvasNode.texturePreview.stableId = canvasNode.stableId;
    AddValueFields(canvasNode, node);
    canvasNode.outputsPerField = !canvasNode.outputs.empty() &&
        canvasNode.outputs.size() == canvasNode.valueFields.size();
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        canvasNode.alignPinRowsAcrossLanes = true;
        const std::size_t outputCount = node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode && canvasNode.outputs.empty()
            ? 1U
            : canvasNode.outputs.size();
        const std::size_t rowCount = std::max(canvasNode.inputs.size(), outputCount);
        if (rowCount > 0U) {
            canvasNode.heightOverride = std::max(
                canvasNode.heightOverride,
                MaterialGraphCanvas::HeaderHeight +
                    MaterialGraphCanvas::BodyTopPadding +
                    (static_cast<float>(rowCount) * MaterialGraphCanvas::PinRowHeight) +
                    MaterialGraphCanvas::BodyTopPadding);
        }
    }
    return canvasNode;
}

MaterialGraphCanvasDocumentBuildResult BuildMaterialGraphCanvasFromDocument(
    const kb::render::RenderMaterialGraphDocument& document) {
    MaterialGraphCanvasDocumentBuildResult result{};
    std::unordered_map<std::uint32_t, std::uint32_t> nodeIndexById;
    nodeIndexById.reserve(document.nodes.size());

    for (const kb::render::RenderMaterialGraphNode& node : document.nodes) {
        const std::uint32_t index = result.canvas.AddNode(BuildMaterialGraphCanvasNode(node));
        nodeIndexById.emplace(node.id, index);
    }

    for (const kb::render::RenderMaterialGraphLink& link : document.links) {
        const auto fromNode = nodeIndexById.find(link.fromNodeId);
        const auto toNode = nodeIndexById.find(link.toNodeId);
        if (fromNode == nodeIndexById.end() || toNode == nodeIndexById.end()) {
            ++result.skippedLinks;
            continue;
        }

        const MaterialGraphCanvasNode* fromCanvasNode = result.canvas.NodeAt(fromNode->second);
        const MaterialGraphCanvasNode* toCanvasNode = result.canvas.NodeAt(toNode->second);
        if (fromCanvasNode == nullptr || toCanvasNode == nullptr) {
            ++result.skippedLinks;
            continue;
        }

        const auto outputIt = std::find_if(
            fromCanvasNode->outputs.begin(),
            fromCanvasNode->outputs.end(),
            [&link](const MaterialGraphCanvasPin& pin) { return pin.stableId == link.fromPin; });
        const auto inputIt = std::find_if(
            toCanvasNode->inputs.begin(),
            toCanvasNode->inputs.end(),
            [&link](const MaterialGraphCanvasPin& pin) { return pin.stableId == link.toPin; });
        if (outputIt == fromCanvasNode->outputs.end() || inputIt == toCanvasNode->inputs.end()) {
            ++result.skippedLinks;
            continue;
        }

        result.canvas.AddLink(MaterialGraphCanvasLink{
            .fromNode = fromNode->second,
            .fromPin = static_cast<std::uint32_t>(std::distance(fromCanvasNode->outputs.begin(), outputIt)),
            .toNode = toNode->second,
            .toPin = static_cast<std::uint32_t>(std::distance(toCanvasNode->inputs.begin(), inputIt)),
            .stableId = std::to_string(link.id),
        });
    }

    return result;
}

} // namespace kb::editor
