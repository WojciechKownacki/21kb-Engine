#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>

namespace kb::render::detail {

// Owns the static pin membership and order for every material graph node kind.
// Compiler IR, public pin queries and validation must derive from this one schema.
class RenderMaterialGraphPinSchema final {
  public:
    RenderMaterialGraphPinSchema() = delete;

    enum class Direction : std::uint8_t {
        Input,
        Output,
    };

    template <typename Visitor> static void VisitStaticPins(RenderMaterialGraphNodeKind kind, Visitor visitor) {
        switch (kind) {
        case RenderMaterialGraphNodeKind::MaterialOutput:
            visitor("baseColor", Direction::Input);
            visitor("metallic", Direction::Input);
            visitor("roughness", Direction::Input);
            visitor("normal", Direction::Input);
            visitor("emissive", Direction::Input);
            visitor("occlusion", Direction::Input);
            visitor("alpha", Direction::Input);
            visitor("alphaClipThreshold", Direction::Input);
            visitor("worldPositionOffset", Direction::Input);
            visitor("specular", Direction::Input);
            visitor("tangentOutput", Direction::Input);
            visitor("attributes", Direction::Input);
            visitor("customizedUv0", Direction::Input);
            visitor("displacement", Direction::Input);
            break;
        case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
            visitor("baseColor", Direction::Input);
            visitor("metallic", Direction::Input);
            visitor("roughness", Direction::Input);
            visitor("normal", Direction::Input);
            visitor("emissive", Direction::Input);
            visitor("occlusion", Direction::Input);
            visitor("alpha", Direction::Input);
            visitor("alphaClipThreshold", Direction::Input);
            visitor("specular", Direction::Input);
            visitor("tangentOutput", Direction::Input);
            visitor("attributes", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
            visitor("attributes", Direction::Input);
            visitor("baseColor", Direction::Output);
            visitor("metallic", Direction::Output);
            visitor("roughness", Direction::Output);
            visitor("normal", Direction::Output);
            visitor("emissive", Direction::Output);
            visitor("occlusion", Direction::Output);
            visitor("alpha", Direction::Output);
            visitor("alphaClipThreshold", Direction::Output);
            visitor("specular", Direction::Output);
            visitor("tangentOutput", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("factor", Direction::Input);
            visitor("attributes", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::GetMaterialAttributes:
            visitor("attributes", Direction::Input);
            visitor("baseColor", Direction::Output);
            visitor("metallic", Direction::Output);
            visitor("roughness", Direction::Output);
            visitor("normal", Direction::Output);
            visitor("emissive", Direction::Output);
            visitor("occlusion", Direction::Output);
            visitor("alpha", Direction::Output);
            visitor("alphaClipThreshold", Direction::Output);
            visitor("specular", Direction::Output);
            visitor("tangentOutput", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::SetMaterialAttributes:
            visitor("attributes", Direction::Input);
            visitor("baseColor", Direction::Input);
            visitor("metallic", Direction::Input);
            visitor("roughness", Direction::Input);
            visitor("normal", Direction::Input);
            visitor("emissive", Direction::Input);
            visitor("occlusion", Direction::Input);
            visitor("alpha", Direction::Input);
            visitor("alphaClipThreshold", Direction::Input);
            visitor("specular", Direction::Input);
            visitor("tangentOutput", Direction::Input);
            visitor("attributesOut", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::StaticBoolParameter:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::StaticSwitch:
            visitor("value", Direction::Input);
            visitor("true", Direction::Input);
            visitor("false", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::StaticComponentMask:
            visitor("input", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::FunctionInput:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::FunctionOutput:
            visitor("value", Direction::Input);
            break;
        case RenderMaterialGraphNodeKind::MaterialFunctionCall:
            break;
        case RenderMaterialGraphNodeKind::LayerStack:
            visitor("attributes", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::QualitySwitch:
            visitor("low", Direction::Input);
            visitor("med", Direction::Input);
            visitor("high", Direction::Input);
            visitor("epic", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
            visitor("es3", Direction::Input);
            visitor("sm5", Direction::Input);
            visitor("sm6", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ShadingPathSwitch:
            visitor("forward", Direction::Input);
            visitor("forwardPlus", Direction::Input);
            visitor("deferred", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ShaderStageSwitch:
            visitor("vertex", Direction::Input);
            visitor("fragment", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::TextureCoordinate:
        case RenderMaterialGraphNodeKind::ViewportUV:
            visitor("uv", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Panner:
        case RenderMaterialGraphNodeKind::Rotator:
            visitor("coordinate", Direction::Input);
            visitor("time", Direction::Input);
            visitor("uv", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::BumpOffset:
            visitor("coordinate", Direction::Input);
            visitor("height", Direction::Input);
            visitor("uv", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ConstantBiasScale:
            visitor("input", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::RotateAboutAxis:
            visitor("axis", Direction::Input);
            visitor("angle", Direction::Input);
            visitor("position", Direction::Input);
            visitor("result", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::CameraPosition:
        case RenderMaterialGraphNodeKind::CameraVector:
        case RenderMaterialGraphNodeKind::ReflectionVector:
        case RenderMaterialGraphNodeKind::LightVector:
        case RenderMaterialGraphNodeKind::PixelNormalWS:
        case RenderMaterialGraphNodeKind::VertexNormalWS:
        case RenderMaterialGraphNodeKind::VertexTangentWS:
        case RenderMaterialGraphNodeKind::ObjectOrientation:
        case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        case RenderMaterialGraphNodeKind::ViewProperty:
        case RenderMaterialGraphNodeKind::ViewSize:
        case RenderMaterialGraphNodeKind::TwoSidedSign:
        case RenderMaterialGraphNodeKind::SceneDepth:
        case RenderMaterialGraphNodeKind::PixelDepth:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::CameraDepthFade:
            visitor("fadeLength", Direction::Input);
            visitor("fadeOffset", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::SceneColor:
        case RenderMaterialGraphNodeKind::SceneTexture:
            visitor("uv", Direction::Input);
            visitor("color", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::DepthFade:
            visitor("fadeDistance", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::TextureSample:
            visitor("texture", Direction::Input);
            visitor("uv", Direction::Input);
            visitor("color", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::TextureSampleCube:
            visitor("texture", Direction::Input);
            visitor("direction", Direction::Input);
            visitor("color", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::TextureSampleVolume:
            visitor("texture", Direction::Input);
            visitor("uvw", Direction::Input);
            visitor("color", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::TextureSample2DArray:
            visitor("texture", Direction::Input);
            visitor("uv", Direction::Input);
            visitor("layer", Direction::Input);
            visitor("color", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Add:
        case RenderMaterialGraphNodeKind::Subtract:
        case RenderMaterialGraphNodeKind::Multiply:
        case RenderMaterialGraphNodeKind::Divide:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Minimum:
        case RenderMaterialGraphNodeKind::Maximum:
        case RenderMaterialGraphNodeKind::DotProduct:
        case RenderMaterialGraphNodeKind::CrossProduct:
        case RenderMaterialGraphNodeKind::Distance:
        case RenderMaterialGraphNodeKind::Fmod:
        case RenderMaterialGraphNodeKind::SphereMask:
        case RenderMaterialGraphNodeKind::AppendVector:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::InverseLerp:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("value", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Power:
            visitor("base", Direction::Input);
            visitor("exponent", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::CustomCode:
            visitor("A", Direction::Input);
            visitor("B", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Reroute:
        case RenderMaterialGraphNodeKind::CompositeInput:
        case RenderMaterialGraphNodeKind::CompositeOutput:
            visitor("input", Direction::Input);
            visitor("output", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
            visitor("input", Direction::Input);
            break;
        case RenderMaterialGraphNodeKind::NamedRerouteUsage:
            visitor("output", Direction::Output);
            break;
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
            visitor("value", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Sobol:
            visitor("cell", Direction::Input);
            visitor("index", Direction::Input);
            visitor("seed", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::BreakVector:
            visitor("value", Direction::Input);
            visitor("x", Direction::Output);
            visitor("y", Direction::Output);
            visitor("z", Direction::Output);
            visitor("w", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::MakeVector:
            visitor("x", Direction::Input);
            visitor("y", Direction::Input);
            visitor("z", Direction::Input);
            visitor("w", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Step:
            visitor("edge", Direction::Input);
            visitor("value", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::SmoothStep:
            visitor("min", Direction::Input);
            visitor("max", Direction::Input);
            visitor("value", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::If:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("less", Direction::Input);
            visitor("equal", Direction::Input);
            visitor("greater", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::RuntimeSwitch:
            visitor("index", Direction::Input);
            visitor("default", Direction::Input);
            visitor("case0", Direction::Input);
            visitor("case1", Direction::Input);
            visitor("case2", Direction::Input);
            visitor("case3", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Desaturate:
            visitor("color", Direction::Input);
            visitor("fraction", Direction::Input);
            visitor("color", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Fresnel:
            visitor("normal", Direction::Input);
            visitor("view", Direction::Input);
            visitor("exponent", Direction::Input);
            visitor("base", Direction::Input);
            visitor("value", Direction::Output);
            break;
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
            visitor("value", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ArcTangent2:
        case RenderMaterialGraphNodeKind::ArcTangent2Fast:
            visitor("y", Direction::Input);
            visitor("x", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Clamp:
            visitor("value", Direction::Input);
            visitor("min", Direction::Input);
            visitor("max", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Lerp:
            visitor("a", Direction::Input);
            visitor("b", Direction::Input);
            visitor("t", Direction::Input);
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::NormalUnpack:
            visitor("color", Direction::Input);
            visitor("normal", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ConstantScalar:
        case RenderMaterialGraphNodeKind::ConstantBool:
        case RenderMaterialGraphNodeKind::ParameterScalar:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ConstantVector2:
            visitor("xy", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ConstantVector:
            visitor("xyz", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ParameterVector:
            visitor("xyz", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ConstantColor:
        case RenderMaterialGraphNodeKind::ParameterColor:
            visitor("rgba", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::CollectionParameter:
            visitor("value", Direction::Output);
            visitor("scalar", Direction::Output);
            visitor("xyz", Direction::Output);
            visitor("rgba", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ParameterTexture:
        case RenderMaterialGraphNodeKind::TextureObject:
        case RenderMaterialGraphNodeKind::TextureObjectCube:
        case RenderMaterialGraphNodeKind::TextureObjectVolume:
        case RenderMaterialGraphNodeKind::TextureObject2DArray:
            visitor("texture", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Uv:
            visitor("uv", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::Time:
        case RenderMaterialGraphNodeKind::DeltaTime:
        case RenderMaterialGraphNodeKind::PerInstanceRandom:
        case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
        case RenderMaterialGraphNodeKind::DistanceCullFade:
        case RenderMaterialGraphNodeKind::PerInstanceCustomData:
        case RenderMaterialGraphNodeKind::ObjectRadius:
        case RenderMaterialGraphNodeKind::ObjectBounds:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::VertexColor:
        case RenderMaterialGraphNodeKind::DynamicParameter:
            visitor("rgba", Direction::Output);
            visitor("r", Direction::Output);
            visitor("g", Direction::Output);
            visitor("b", Direction::Output);
            visitor("a", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::ScreenPosition:
        case RenderMaterialGraphNodeKind::PixelPosition:
            visitor("xy", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::LocalPosition:
        case RenderMaterialGraphNodeKind::ObjectPosition:
        case RenderMaterialGraphNodeKind::WorldPosition:
            visitor("xyz", Direction::Output);
            break;
        }
    }

    // Accepted names kept for serialized graphs and scripting compatibility.
    // They are intentionally not emitted as separate visible pins in compiler IR.
    template <typename Visitor>
    static void VisitCompatibilityAliases(RenderMaterialGraphNodeKind kind, Visitor visitor) {
        switch (kind) {
        case RenderMaterialGraphNodeKind::LocalPosition:
        case RenderMaterialGraphNodeKind::ObjectPosition:
        case RenderMaterialGraphNodeKind::WorldPosition:
            visitor("value", Direction::Output);
            break;
        case RenderMaterialGraphNodeKind::PreSkinnedPosition:
            visitor("xyz", Direction::Output);
            break;
        default:
            break;
        }
    }
};

} // namespace kb::render::detail
