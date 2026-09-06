#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "private/resources/material_graph/RenderMaterialGraphPinSchema.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] RenderMaterialGraphPinType PassThroughPinTypeFromHint(std::string_view hint) noexcept {
    if (const std::optional<RenderMaterialGraphPinType> parsed = ParseRenderMaterialGraphPinType(hint)) {
        if (*parsed != RenderMaterialGraphPinType::Unknown) {
            return *parsed;
        }
    }
    return RenderMaterialGraphPinType::Float4;
}

[[nodiscard]] RenderMaterialGraphPinType PassThroughPinType(const RenderMaterialGraphNode& node) noexcept {
    return PassThroughPinTypeFromHint(node.parameter.defaultValueHint);
}

[[nodiscard]] RenderMaterialGraphPinType FunctionEndpointPinType(const RenderMaterialGraphNode& node) noexcept {
    return PassThroughPinTypeFromHint(node.parameter.defaultValueHint);
}

[[nodiscard]] bool IsStaticPin(
    RenderMaterialGraphNodeKind kind,
    std::string_view name,
    detail::RenderMaterialGraphPinSchema::Direction direction) noexcept {
    bool found = false;
    const auto match = [&found, name, direction](
                           std::string_view candidate,
                           detail::RenderMaterialGraphPinSchema::Direction candidateDirection) noexcept {
        found = found || (candidateDirection == direction && candidate == name);
    };
    detail::RenderMaterialGraphPinSchema::VisitStaticPins(kind, match);
    detail::RenderMaterialGraphPinSchema::VisitCompatibilityAliases(kind, match);
    return found;
}

} // namespace

bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return IsStaticPin(kind, pin, detail::RenderMaterialGraphPinSchema::Direction::Input);
}

bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    return IsStaticPin(kind, pin, detail::RenderMaterialGraphPinSchema::Direction::Output);
}

bool IsRenderMaterialGraphInputPin(const RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return std::any_of(node.customCode.inputs.begin(), node.customCode.inputs.end(), [pin](const RenderMaterialGraphCustomPin& customPin) {
            return customPin.name == pin;
        });
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
        return pin == "value";
    }
    if (node.kind != RenderMaterialGraphNodeKind::CustomCode) {
        return IsRenderMaterialGraphInputPin(node.kind, pin);
    }
    return std::any_of(node.customCode.inputs.begin(), node.customCode.inputs.end(), [pin](const RenderMaterialGraphCustomPin& customPin) {
        return customPin.name == pin;
    });
}

bool IsRenderMaterialGraphOutputPin(const RenderMaterialGraphNode& node, std::string_view pin) noexcept {
    if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return std::any_of(node.customCode.outputs.begin(), node.customCode.outputs.end(), [pin](const RenderMaterialGraphCustomPin& customPin) {
            return customPin.name == pin;
        });
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionInput) {
        return pin == "value";
    }
    if (node.kind != RenderMaterialGraphNodeKind::CustomCode) {
        return IsRenderMaterialGraphOutputPin(node.kind, pin);
    }
    if (pin == "value") {
        return true;
    }
    return std::any_of(node.customCode.outputs.begin(), node.customCode.outputs.end(), [pin](const RenderMaterialGraphCustomPin& customPin) {
        return customPin.name == pin;
    });
}

RenderMaterialGraphPinType RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        if (outputPin) return RenderMaterialGraphPinType::Unknown;
        if (pin == "attributes") return RenderMaterialGraphPinType::MaterialAttributes;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "customizedUv0") return RenderMaterialGraphPinType::Float2;
        if (pin == "worldPositionOffset" || pin == "tangentOutput" || pin == "displacement") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        if (outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "tangentOutput") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        if (!outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "tangentOutput") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        if (outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "a" || pin == "b") return RenderMaterialGraphPinType::MaterialAttributes;
        if (pin == "factor") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        if (!outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "tangentOutput") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        if (outputPin) return pin == "attributesOut" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "attributes") return RenderMaterialGraphPinType::MaterialAttributes;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "tangentOutput") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::StaticSwitch:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "value") return RenderMaterialGraphPinType::Float;
        if (pin == "true" || pin == "false") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "input") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::QualitySwitch:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "low" || pin == "med" || pin == "high" || pin == "epic") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "es3" || pin == "sm5" || pin == "sm6") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "forward" || pin == "forwardPlus" || pin == "deferred") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "vertex" || pin == "fragment") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::ViewportUV:
        return (outputPin && pin == "uv") ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
        if (outputPin) return pin == "uv" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
        if (pin == "coordinate") return RenderMaterialGraphPinType::Float2;
        if (pin == "time") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::BumpOffset:
        if (outputPin) return pin == "uv" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
        if (pin == "coordinate") return RenderMaterialGraphPinType::Float2;
        if (pin == "height") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
        if (pin == "input") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        if (outputPin) return pin == "result" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
        if (pin == "axis" || pin == "position") return RenderMaterialGraphPinType::Float3;
        if (pin == "angle") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::CameraPosition:
    case RenderMaterialGraphNodeKind::CameraVector:
    case RenderMaterialGraphNodeKind::ReflectionVector:
    case RenderMaterialGraphNodeKind::LightVector:
    case RenderMaterialGraphNodeKind::PixelNormalWS:
    case RenderMaterialGraphNodeKind::VertexNormalWS:
    case RenderMaterialGraphNodeKind::VertexTangentWS:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TwoSidedSign:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::PixelDepth:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::CameraDepthFade:
        if (outputPin) return pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
        if (pin == "fadeLength" || pin == "fadeOffset") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        if (!outputPin && pin == "uv") return RenderMaterialGraphPinType::Float2;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::DepthFade:
        if (outputPin) return pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
        if (pin == "fadeDistance") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        if (!outputPin && pin == "input") return RenderMaterialGraphPinType::Float4;
        if (outputPin && pin == "output") return RenderMaterialGraphPinType::Float4;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return (!outputPin && pin == "input") ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return (outputPin && pin == "output") ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::FunctionInput:
        return (outputPin && pin == "value") ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::FunctionOutput:
        return (!outputPin && pin == "value") ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::LayerStack:
        return (outputPin && pin == "attributes") ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureSample:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::Texture2D;
        if (!outputPin && pin == "uv") return RenderMaterialGraphPinType::Float2;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::TextureCube;
        if (!outputPin && pin == "direction") return RenderMaterialGraphPinType::Float3;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::Texture3D;
        if (!outputPin && pin == "uvw") return RenderMaterialGraphPinType::Float3;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::Texture2DArray;
        if (!outputPin && pin == "uv") return RenderMaterialGraphPinType::Float2;
        if (!outputPin && pin == "layer") return RenderMaterialGraphPinType::Float;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantBool:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Bool : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return outputPin && pin == "xy" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantVector:
        if (!outputPin) return RenderMaterialGraphPinType::Unknown;
        if (pin == "xyz") return RenderMaterialGraphPinType::Float3;
        if (pin == "r" || pin == "g" || pin == "b") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return outputPin && pin == "xyz" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        if (!outputPin) return RenderMaterialGraphPinType::Unknown;
        if (pin == "rgba") return RenderMaterialGraphPinType::Color;
        if (pin == "r" || pin == "g" || pin == "b" || pin == "a") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        if (!outputPin) return RenderMaterialGraphPinType::Unknown;
        if (pin == "scalar" || pin == "r" || pin == "g" || pin == "b" || pin == "a") return RenderMaterialGraphPinType::Float;
        if (pin == "xyz") return RenderMaterialGraphPinType::Float3;
        if (pin == "rgba" || pin == "value") return RenderMaterialGraphPinType::Color;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::Texture2D : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureObject:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::Texture2D : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureObjectCube:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::TextureCube : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::Texture3D : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::Texture2DArray : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Fmod:
    case RenderMaterialGraphNodeKind::SphereMask:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::AppendVector:
        if (!outputPin && pin == "a") return RenderMaterialGraphPinType::Float3;
        if (!outputPin && pin == "b") return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::InverseLerp:
        if (!outputPin && (pin == "a" || pin == "b" || pin == "value")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Power:
        if (!outputPin && (pin == "base" || pin == "exponent")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::CustomCode:
        if (!outputPin && (pin == "A" || pin == "B")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Sobol:
        if (!outputPin && (pin == "cell" || pin == "seed")) return RenderMaterialGraphPinType::Float2;
        if (!outputPin && pin == "index") return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
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
    case RenderMaterialGraphNodeKind::PartialDerivativeX:
    case RenderMaterialGraphNodeKind::PartialDerivativeY:
    case RenderMaterialGraphNodeKind::BlackBody:
    case RenderMaterialGraphNodeKind::Noise:
    case RenderMaterialGraphNodeKind::VectorNoise:
    case RenderMaterialGraphNodeKind::ColorRamp:
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case RenderMaterialGraphNodeKind::Transform:
    case RenderMaterialGraphNodeKind::TransformPosition:
    case RenderMaterialGraphNodeKind::Cosine:
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::Distance:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float3;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::CrossProduct:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float3;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Normalize:
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float3;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Length:
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float3;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::BreakVector:
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float4;
        return outputPin && (pin == "x" || pin == "y" || pin == "z" || pin == "w") ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::MakeVector:
        if (!outputPin && (pin == "x" || pin == "y" || pin == "z" || pin == "w")) return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Step:
        if (!outputPin && (pin == "edge" || pin == "value")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::SmoothStep:
        if (!outputPin && (pin == "min" || pin == "max" || pin == "value")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::If:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float;
        if (!outputPin && (pin == "less" || pin == "equal" || pin == "greater")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        if (!outputPin && pin == "index") return RenderMaterialGraphPinType::Float;
        if (!outputPin && (pin == "default" || pin == "case0" || pin == "case1" || pin == "case2" || pin == "case3")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Desaturate:
        if (!outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (!outputPin && pin == "fraction") return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "color" ? RenderMaterialGraphPinType::Color : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Fresnel:
        if (!outputPin && (pin == "normal" || pin == "view")) return RenderMaterialGraphPinType::Float3;
        if (!outputPin && (pin == "exponent" || pin == "base")) return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
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
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
        if (!outputPin && (pin == "y" || pin == "x")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Clamp:
        if (!outputPin && (pin == "value" || pin == "min" || pin == "max")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Lerp:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float4;
        if (!outputPin && pin == "t") return RenderMaterialGraphPinType::Float;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        if (!outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        return outputPin && pin == "normal" ? RenderMaterialGraphPinType::Normal : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Uv:
        return outputPin && pin == "uv" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::DeltaTime:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
    case RenderMaterialGraphNodeKind::DistanceCullFade:
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::DynamicParameter:
        if (outputPin && pin == "rgba") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ObjectBounds:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ObjectOrientation:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        return outputPin && (pin == "value" || pin == "xyz") ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::VertexColor:
        if (outputPin && pin == "rgba") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::PixelPosition:
        return outputPin && pin == "xy" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        return outputPin && (pin == "value" || pin == "xyz") ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    }
    return RenderMaterialGraphPinType::Unknown;
}

RenderMaterialGraphPinType RenderMaterialGraphPinDataType(const RenderMaterialGraphNode& node, std::string_view pin, bool outputPin) noexcept {
    if (node.kind == RenderMaterialGraphNodeKind::FunctionInput) {
        return (outputPin && pin == "value") ? FunctionEndpointPinType(node) : RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
        return (!outputPin && pin == "value") ? FunctionEndpointPinType(node) : RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        const std::vector<RenderMaterialGraphCustomPin>& pins = outputPin ? node.customCode.outputs : node.customCode.inputs;
        for (const RenderMaterialGraphCustomPin& customPin : pins) {
            if (customPin.name == pin) {
                return customPin.type;
            }
        }
        return RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind == RenderMaterialGraphNodeKind::Reroute ||
        node.kind == RenderMaterialGraphNodeKind::CompositeInput ||
        node.kind == RenderMaterialGraphNodeKind::CompositeOutput) {
        if ((!outputPin && pin == "input") || (outputPin && pin == "output")) {
            return PassThroughPinType(node);
        }
        return RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration) {
        return (!outputPin && pin == "input") ? PassThroughPinType(node) : RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage) {
        return (outputPin && pin == "output") ? PassThroughPinType(node) : RenderMaterialGraphPinType::Unknown;
    }
    if (node.kind != RenderMaterialGraphNodeKind::CustomCode) {
        return RenderMaterialGraphPinDataType(node.kind, pin, outputPin);
    }
    if (outputPin) {
        if (pin == "value") {
            return node.customCode.outputType;
        }
        for (const RenderMaterialGraphCustomPin& customPin : node.customCode.outputs) {
            if (customPin.name == pin) {
                return customPin.type;
            }
        }
        return RenderMaterialGraphPinType::Unknown;
    }
    for (const RenderMaterialGraphCustomPin& customPin : node.customCode.inputs) {
        if (customPin.name == pin) {
            return customPin.type;
        }
    }
    return RenderMaterialGraphPinType::Unknown;
}

} // namespace kb::render
