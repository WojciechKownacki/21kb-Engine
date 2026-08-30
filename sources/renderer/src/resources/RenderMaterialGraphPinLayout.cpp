#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

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

} // namespace

bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return pin == "baseColor" ||
            pin == "metallic" ||
            pin == "roughness" ||
            pin == "normal" ||
            pin == "emissive" ||
            pin == "occlusion" ||
            pin == "alpha" ||
            pin == "alphaClipThreshold" ||
            pin == "worldPositionOffset" ||
            pin == "specular" ||
            pin == "tangentOutput" ||
            pin == "attributes" ||
            pin == "customizedUv0" ||
            pin == "displacement";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" ||
            pin == "alpha" || pin == "alphaClipThreshold" || pin == "specular" ||
            pin == "tangentOutput";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return pin == "a" || pin == "b" || pin == "factor";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return pin == "attributes" || pin == "baseColor" || pin == "metallic" || pin == "roughness" ||
            pin == "normal" || pin == "emissive" ||
            pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" || pin == "specular" ||
            pin == "tangentOutput";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return pin == "value" || pin == "true" || pin == "false";
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return pin == "input";
    case RenderMaterialGraphNodeKind::QualitySwitch:
        return pin == "low" || pin == "med" || pin == "high" || pin == "epic";
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return pin == "es3" || pin == "sm5" || pin == "sm6";
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return pin == "forward" || pin == "forwardPlus" || pin == "deferred";
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return pin == "vertex" || pin == "fragment";
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
        return pin == "coordinate" || pin == "time";
    case RenderMaterialGraphNodeKind::BumpOffset:
        return pin == "coordinate" || pin == "height";
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        return pin == "input";
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        return pin == "axis" || pin == "angle" || pin == "position";
    case RenderMaterialGraphNodeKind::DepthFade:
        return pin == "fadeDistance";
    case RenderMaterialGraphNodeKind::CameraDepthFade:
        return pin == "fadeLength" || pin == "fadeOffset";
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return pin == "input";
    case RenderMaterialGraphNodeKind::FunctionOutput:
        return pin == "value";
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
    case RenderMaterialGraphNodeKind::LayerStack:
        return false;
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "texture" || pin == "uv";
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        return pin == "texture" || pin == "direction";
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        return pin == "texture" || pin == "uvw";
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return pin == "texture" || pin == "uv" || pin == "layer";
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        return pin == "uv";
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::Fmod:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
        return pin == "a" || pin == "b";
    case RenderMaterialGraphNodeKind::InverseLerp:
        return pin == "a" || pin == "b" || pin == "value";
    case RenderMaterialGraphNodeKind::Power:
        return pin == "base" || pin == "exponent";
    case RenderMaterialGraphNodeKind::CustomCode:
        return pin == "A" || pin == "B";
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
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::BreakVector:
        return pin == "value";
    case RenderMaterialGraphNodeKind::MakeVector:
        return pin == "x" || pin == "y" || pin == "z" || pin == "w";
    case RenderMaterialGraphNodeKind::Step:
        return pin == "edge" || pin == "value";
    case RenderMaterialGraphNodeKind::SmoothStep:
        return pin == "min" || pin == "max" || pin == "value";
    case RenderMaterialGraphNodeKind::If:
        return pin == "a" || pin == "b" || pin == "less" || pin == "equal" || pin == "greater";
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        return pin == "index" || pin == "default" || pin == "case0" || pin == "case1" || pin == "case2" || pin == "case3";
    case RenderMaterialGraphNodeKind::Sobol:
        return pin == "cell" || pin == "index" || pin == "seed";
    case RenderMaterialGraphNodeKind::Desaturate:
        return pin == "color" || pin == "fraction";
    case RenderMaterialGraphNodeKind::Fresnel:
        return pin == "normal" || pin == "view" || pin == "exponent" || pin == "base";
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
        return pin == "value";
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return pin == "y" || pin == "x";
    case RenderMaterialGraphNodeKind::Clamp:
        return pin == "value" || pin == "min" || pin == "max";
    case RenderMaterialGraphNodeKind::Lerp:
        return pin == "a" || pin == "b" || pin == "t";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return pin == "color";
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::CollectionParameter:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::DeltaTime:
    case RenderMaterialGraphNodeKind::DynamicParameter:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::PixelPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
    case RenderMaterialGraphNodeKind::DistanceCullFade:
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::ObjectBounds:
    case RenderMaterialGraphNodeKind::ObjectOrientation:
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
    case RenderMaterialGraphNodeKind::ViewportUV:
    case RenderMaterialGraphNodeKind::CameraPosition:
    case RenderMaterialGraphNodeKind::CameraVector:
    case RenderMaterialGraphNodeKind::ReflectionVector:
    case RenderMaterialGraphNodeKind::LightVector:
    case RenderMaterialGraphNodeKind::PixelNormalWS:
    case RenderMaterialGraphNodeKind::VertexNormalWS:
    case RenderMaterialGraphNodeKind::VertexTangentWS:
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
    case RenderMaterialGraphNodeKind::TwoSidedSign:
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::PixelDepth:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::FunctionInput:
        return false;
    }
    return false;
}

bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::ParameterScalar:
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
    case RenderMaterialGraphNodeKind::Sobol:
    case RenderMaterialGraphNodeKind::ColorRamp:
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case RenderMaterialGraphNodeKind::Transform:
    case RenderMaterialGraphNodeKind::TransformPosition:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
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
    case RenderMaterialGraphNodeKind::Fmod:
    case RenderMaterialGraphNodeKind::InverseLerp:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
        return pin == "value";
    case RenderMaterialGraphNodeKind::Desaturate:
        return pin == "color";
    case RenderMaterialGraphNodeKind::BreakVector:
        return pin == "x" || pin == "y" || pin == "z" || pin == "w";
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return pin == "xy";
    case RenderMaterialGraphNodeKind::ConstantVector:
        return pin == "xyz" || pin == "r" || pin == "g" || pin == "b";
    case RenderMaterialGraphNodeKind::ParameterVector:
        return pin == "xyz";
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return pin == "rgba" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        return pin == "color" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::VertexColor:
        return pin == "rgba" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return pin == "value" || pin == "scalar" || pin == "xyz" || pin == "rgba" ||
            pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::PixelPosition:
        return pin == "xy";
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        return pin == "value" || pin == "xyz";
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return pin == "texture";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return pin == "normal";
    case RenderMaterialGraphNodeKind::Uv:
        return pin == "uv";
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::DeltaTime:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
    case RenderMaterialGraphNodeKind::DistanceCullFade:
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::ObjectBounds:
    case RenderMaterialGraphNodeKind::ObjectOrientation:
        return pin == "value";
    case RenderMaterialGraphNodeKind::DynamicParameter:
        return pin == "rgba" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        return pin == "value" || pin == "xyz";
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        return pin == "value";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" ||
            pin == "alpha" || pin == "alphaClipThreshold" || pin == "specular" ||
            pin == "tangentOutput";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" ||
            pin == "alpha" || pin == "alphaClipThreshold" || pin == "specular" ||
            pin == "tangentOutput";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return pin == "attributesOut";
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return pin == "value";
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        return pin == "result";
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
    case RenderMaterialGraphNodeKind::BumpOffset:
    case RenderMaterialGraphNodeKind::ViewportUV:
        return pin == "uv";
    case RenderMaterialGraphNodeKind::CameraPosition:
    case RenderMaterialGraphNodeKind::CameraVector:
    case RenderMaterialGraphNodeKind::ReflectionVector:
    case RenderMaterialGraphNodeKind::LightVector:
    case RenderMaterialGraphNodeKind::PixelNormalWS:
    case RenderMaterialGraphNodeKind::VertexNormalWS:
    case RenderMaterialGraphNodeKind::VertexTangentWS:
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
    case RenderMaterialGraphNodeKind::TwoSidedSign:
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::PixelDepth:
    case RenderMaterialGraphNodeKind::CameraDepthFade:
    case RenderMaterialGraphNodeKind::DepthFade:
        return pin == "value";
    case RenderMaterialGraphNodeKind::CustomCode:
        return pin == "value";
    case RenderMaterialGraphNodeKind::FunctionInput:
        return pin == "value";
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return false;
    case RenderMaterialGraphNodeKind::LayerStack:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return pin == "output";
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::FunctionOutput:
        return false;
    }
    return false;
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
