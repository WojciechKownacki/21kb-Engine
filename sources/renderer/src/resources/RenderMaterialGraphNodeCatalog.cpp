#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace kb::render {

std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return "MaterialOutput";
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return "ConstantScalar";
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return "ConstantVector2";
    case RenderMaterialGraphNodeKind::ConstantVector:
        return "ConstantVector";
    case RenderMaterialGraphNodeKind::ConstantColor:
        return "ConstantColor";
    case RenderMaterialGraphNodeKind::ConstantBool:
        return "ConstantBool";
    case RenderMaterialGraphNodeKind::TextureSample:
        return "TextureSample";
    case RenderMaterialGraphNodeKind::TextureObject:
        return "TextureObject";
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        return "TextureSampleCube";
    case RenderMaterialGraphNodeKind::TextureObjectCube:
        return "TextureObjectCube";
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        return "TextureSampleVolume";
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
        return "TextureObjectVolume";
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return "TextureSample2DArray";
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return "TextureObject2DArray";
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "ParameterScalar";
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "ParameterVector";
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "ParameterColor";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "ParameterTexture";
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return "CollectionParameter";
    case RenderMaterialGraphNodeKind::Add:
        return "Add";
    case RenderMaterialGraphNodeKind::Subtract:
        return "Subtract";
    case RenderMaterialGraphNodeKind::Multiply:
        return "Multiply";
    case RenderMaterialGraphNodeKind::Divide:
        return "Divide";
    case RenderMaterialGraphNodeKind::Power:
        return "Power";
    case RenderMaterialGraphNodeKind::OneMinus:
        return "OneMinus";
    case RenderMaterialGraphNodeKind::Absolute:
        return "Absolute";
    case RenderMaterialGraphNodeKind::Minimum:
        return "Minimum";
    case RenderMaterialGraphNodeKind::Maximum:
        return "Maximum";
    case RenderMaterialGraphNodeKind::Saturate:
        return "Saturate";
    case RenderMaterialGraphNodeKind::Floor:
        return "Floor";
    case RenderMaterialGraphNodeKind::Ceil:
        return "Ceil";
    case RenderMaterialGraphNodeKind::Fraction:
        return "Fraction";
    case RenderMaterialGraphNodeKind::SquareRoot:
        return "SquareRoot";
    case RenderMaterialGraphNodeKind::Sine:
        return "Sine";
    case RenderMaterialGraphNodeKind::Cosine:
        return "Cosine";
    case RenderMaterialGraphNodeKind::Exponential:
        return "Exponential";
    case RenderMaterialGraphNodeKind::Exponential2:
        return "Exponential2";
    case RenderMaterialGraphNodeKind::Logarithm:
        return "Logarithm";
    case RenderMaterialGraphNodeKind::Logarithm2:
        return "Logarithm2";
    case RenderMaterialGraphNodeKind::SrgbToLinear:
        return "SrgbToLinear";
    case RenderMaterialGraphNodeKind::LinearToSrgb:
        return "LinearToSrgb";
    case RenderMaterialGraphNodeKind::Logarithm10:
        return "Logarithm10";
    case RenderMaterialGraphNodeKind::HsvToRgb:
        return "HsvToRgb";
    case RenderMaterialGraphNodeKind::RgbToHsv:
        return "RgbToHsv";
    case RenderMaterialGraphNodeKind::DeriveNormalZ:
        return "DeriveNormalZ";
    case RenderMaterialGraphNodeKind::Fmod:
        return "Fmod";
    case RenderMaterialGraphNodeKind::InverseLerp:
        return "InverseLerp";
    case RenderMaterialGraphNodeKind::PartialDerivativeX:
        return "PartialDerivativeX";
    case RenderMaterialGraphNodeKind::PartialDerivativeY:
        return "PartialDerivativeY";
    case RenderMaterialGraphNodeKind::SphereMask:
        return "SphereMask";
    case RenderMaterialGraphNodeKind::BlackBody:
        return "BlackBody";
    case RenderMaterialGraphNodeKind::Noise:
        return "Noise";
    case RenderMaterialGraphNodeKind::VectorNoise:
        return "VectorNoise";
    case RenderMaterialGraphNodeKind::Sobol:
        return "Sobol";
    case RenderMaterialGraphNodeKind::AppendVector:
        return "AppendVector";
    case RenderMaterialGraphNodeKind::ColorRamp:
        return "ColorRamp";
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        return "AntialiasedTextureMask";
    case RenderMaterialGraphNodeKind::Transform:
        return "Transform";
    case RenderMaterialGraphNodeKind::TransformPosition:
        return "TransformPosition";
    case RenderMaterialGraphNodeKind::DotProduct:
        return "DotProduct";
    case RenderMaterialGraphNodeKind::CrossProduct:
        return "CrossProduct";
    case RenderMaterialGraphNodeKind::Normalize:
        return "Normalize";
    case RenderMaterialGraphNodeKind::Length:
        return "Length";
    case RenderMaterialGraphNodeKind::Distance:
        return "Distance";
    case RenderMaterialGraphNodeKind::BreakVector:
        return "BreakVector";
    case RenderMaterialGraphNodeKind::MakeVector:
        return "MakeVector";
    case RenderMaterialGraphNodeKind::Step:
        return "Step";
    case RenderMaterialGraphNodeKind::SmoothStep:
        return "SmoothStep";
    case RenderMaterialGraphNodeKind::If:
        return "If";
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        return "Switch";
    case RenderMaterialGraphNodeKind::Desaturate:
        return "Desaturate";
    case RenderMaterialGraphNodeKind::Fresnel:
        return "Fresnel";
    case RenderMaterialGraphNodeKind::Negate:
        return "Negate";
    case RenderMaterialGraphNodeKind::Sign:
        return "Sign";
    case RenderMaterialGraphNodeKind::Round:
        return "Round";
    case RenderMaterialGraphNodeKind::Truncate:
        return "Truncate";
    case RenderMaterialGraphNodeKind::Tangent:
        return "Tangent";
    case RenderMaterialGraphNodeKind::ArcSine:
        return "ArcSine";
    case RenderMaterialGraphNodeKind::ArcCosine:
        return "ArcCosine";
    case RenderMaterialGraphNodeKind::ArcTangent:
        return "ArcTangent";
    case RenderMaterialGraphNodeKind::ArcTangent2:
        return "ArcTangent2";
    case RenderMaterialGraphNodeKind::ArcSineFast:
        return "ArcSineFast";
    case RenderMaterialGraphNodeKind::ArcCosineFast:
        return "ArcCosineFast";
    case RenderMaterialGraphNodeKind::ArcTangentFast:
        return "ArcTangentFast";
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return "ArcTangent2Fast";
    case RenderMaterialGraphNodeKind::Clamp:
        return "Clamp";
    case RenderMaterialGraphNodeKind::Lerp:
        return "Lerp";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return "NormalUnpack";
    case RenderMaterialGraphNodeKind::Uv:
        return "UV";
    case RenderMaterialGraphNodeKind::Time:
        return "Time";
    case RenderMaterialGraphNodeKind::DeltaTime:
        return "DeltaTime";
    case RenderMaterialGraphNodeKind::DynamicParameter:
        return "DynamicParameter";
    case RenderMaterialGraphNodeKind::VertexColor:
        return "VertexColor";
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return "ScreenPosition";
    case RenderMaterialGraphNodeKind::PixelPosition:
        return "PixelPosition";
    case RenderMaterialGraphNodeKind::LocalPosition:
        return "LocalPosition";
    case RenderMaterialGraphNodeKind::ObjectPosition:
        return "ObjectPosition";
    case RenderMaterialGraphNodeKind::WorldPosition:
        return "WorldPosition";
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
        return "PerInstanceRandom";
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
        return "PerInstanceFadeAmount";
    case RenderMaterialGraphNodeKind::DistanceCullFade:
        return "DistanceCullFade";
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
        return "PerInstanceCustomData";
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return "ObjectRadius";
    case RenderMaterialGraphNodeKind::ObjectBounds:
        return "ObjectBounds";
    case RenderMaterialGraphNodeKind::ObjectOrientation:
        return "ObjectOrientation";
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        return "PreSkinnedPosition";
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        return "PreSkinnedNormal";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return "MakeMaterialAttributes";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return "BreakMaterialAttributes";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return "BlendMaterialAttributes";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return "GetMaterialAttributes";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return "SetMaterialAttributes";
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return "StaticBoolParameter";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return "StaticSwitch";
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return "StaticComponentMask";
    case RenderMaterialGraphNodeKind::QualitySwitch:
        return "QualitySwitch";
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return "FeatureLevelSwitch";
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return "ShadingPathSwitch";
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return "ShaderStageSwitch";
    case RenderMaterialGraphNodeKind::TextureCoordinate:
        return "TextureCoordinate";
    case RenderMaterialGraphNodeKind::Panner:
        return "Panner";
    case RenderMaterialGraphNodeKind::Rotator:
        return "Rotator";
    case RenderMaterialGraphNodeKind::BumpOffset:
        return "BumpOffset";
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        return "ConstantBiasScale";
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        return "RotateAboutAxis";
    case RenderMaterialGraphNodeKind::ViewportUV:
        return "ViewportUV";
    case RenderMaterialGraphNodeKind::CameraPosition:
        return "CameraPosition";
    case RenderMaterialGraphNodeKind::CameraVector:
        return "CameraVector";
    case RenderMaterialGraphNodeKind::ReflectionVector:
        return "ReflectionVector";
    case RenderMaterialGraphNodeKind::LightVector:
        return "LightVector";
    case RenderMaterialGraphNodeKind::PixelNormalWS:
        return "PixelNormalWS";
    case RenderMaterialGraphNodeKind::VertexNormalWS:
        return "VertexNormalWS";
    case RenderMaterialGraphNodeKind::VertexTangentWS:
        return "VertexTangentWS";
    case RenderMaterialGraphNodeKind::ViewProperty:
        return "ViewProperty";
    case RenderMaterialGraphNodeKind::ViewSize:
        return "ViewSize";
    case RenderMaterialGraphNodeKind::TwoSidedSign:
        return "TwoSidedSign";
    case RenderMaterialGraphNodeKind::SceneDepth:
        return "SceneDepth";
    case RenderMaterialGraphNodeKind::PixelDepth:
        return "PixelDepth";
    case RenderMaterialGraphNodeKind::CameraDepthFade:
        return "CameraDepthFade";
    case RenderMaterialGraphNodeKind::SceneColor:
        return "SceneColor";
    case RenderMaterialGraphNodeKind::SceneTexture:
        return "SceneTexture";
    case RenderMaterialGraphNodeKind::DepthFade:
        return "DepthFade";
    case RenderMaterialGraphNodeKind::CustomCode:
        return "CustomCode";
    case RenderMaterialGraphNodeKind::Reroute:
        return "Reroute";
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return "NamedRerouteDeclaration";
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return "NamedRerouteUsage";
    case RenderMaterialGraphNodeKind::CompositeInput:
        return "CompositeInput";
    case RenderMaterialGraphNodeKind::CompositeOutput:
        return "CompositeOutput";
    case RenderMaterialGraphNodeKind::FunctionInput:
        return "FunctionInput";
    case RenderMaterialGraphNodeKind::FunctionOutput:
        return "FunctionOutput";
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return "MaterialFunctionCall";
    case RenderMaterialGraphNodeKind::LayerStack:
        return "LayerStack";
    }
    return "MaterialOutput";
}

RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportStatus(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::CollectionParameter:
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
    case RenderMaterialGraphNodeKind::CustomCode:
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
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
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
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
    case RenderMaterialGraphNodeKind::BumpOffset:
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
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
    case RenderMaterialGraphNodeKind::CameraDepthFade:
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
    case RenderMaterialGraphNodeKind::DepthFade:
    case RenderMaterialGraphNodeKind::Fmod:
    case RenderMaterialGraphNodeKind::InverseLerp:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
    case RenderMaterialGraphNodeKind::FunctionInput:
    case RenderMaterialGraphNodeKind::FunctionOutput:
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
    case RenderMaterialGraphNodeKind::LayerStack:
        return RenderMaterialGraphNodeSupport::Production;
    }
    return RenderMaterialGraphNodeSupport::Unsupported;
}


static std::string_view RenderMaterialGraphNodeSupportMatrixNote(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::DepthFade:
        return "Requires the transparent pass scene-depth binding; GpuDeferred accepts transparent materials and rejects opaque/masked GBuffer geometry.";
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        return "Requires the transparent pass scene-color snapshot binding; GpuDeferred accepts transparent materials and rejects opaque/masked GBuffer geometry.";
    case RenderMaterialGraphNodeKind::CustomCode:
        return "Production when custom pin declarations validate; invalid code reports shader-generation diagnostics.";
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
    case RenderMaterialGraphNodeKind::LayerStack:
        return "Expanded before codegen; dependency changes participate in variant/cache identity.";
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return "Baked into source/variant identity through RenderMaterialGraphBuildContext.";
    default:
        return "Production on GpuForward/Preview/GpuDeferred; deferred requires the GBuffer graph artifact, MRT writer and deferred lighting pass.";
    }
}

std::vector<RenderMaterialGraphNodeSupportMatrixEntry> BuildRenderMaterialGraphNodeSupportMatrix() {
    const std::span<const RenderMaterialGraphNodeKind> kinds = AllRenderMaterialGraphNodeKinds();
    std::vector<RenderMaterialGraphNodeSupportMatrixEntry> matrix;
    matrix.reserve(kinds.size());
    for (const RenderMaterialGraphNodeKind kind : kinds) {
        matrix.push_back(RenderMaterialGraphNodeSupportMatrixEntry{
            .kind = kind,
            .authoringSupport = RenderMaterialGraphNodeSupportStatus(kind),
            .gpuForwardSupport = RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuForward),
            .gpuShadowSupport = RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuShadow),
            .gpuDeferredSupport = RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuDeferred),
            .cpuFallbackSupport = RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::CpuFallback),
            .previewSupport = RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::Preview),
            .note = RenderMaterialGraphNodeSupportMatrixNote(kind),
        });
    }
    return matrix;
}

std::string_view RenderMaterialGraphNodeSupportShortTag(RenderMaterialGraphNodeKind kind) noexcept {
    switch (RenderMaterialGraphNodeSupportStatus(kind)) {
    case RenderMaterialGraphNodeSupport::Production:
        return "";
    case RenderMaterialGraphNodeSupport::Experimental:
        return "EXP";
    case RenderMaterialGraphNodeSupport::FallbackOnly:
        return "FALLBACK";
    case RenderMaterialGraphNodeSupport::Unsupported:
        return "UNSUPPORTED";
    }
    return "UNSUPPORTED";
}

std::span<const RenderMaterialGraphNodeKind> AllRenderMaterialGraphNodeKinds() noexcept {
    static constexpr RenderMaterialGraphNodeKind kKinds[] = {
        RenderMaterialGraphNodeKind::MaterialOutput,
        RenderMaterialGraphNodeKind::ConstantScalar,
        RenderMaterialGraphNodeKind::ConstantVector,
        RenderMaterialGraphNodeKind::ConstantColor,
        RenderMaterialGraphNodeKind::TextureSample,
        RenderMaterialGraphNodeKind::TextureObject,
        RenderMaterialGraphNodeKind::TextureSampleCube,
        RenderMaterialGraphNodeKind::TextureObjectCube,
        RenderMaterialGraphNodeKind::TextureSampleVolume,
        RenderMaterialGraphNodeKind::TextureObjectVolume,
        RenderMaterialGraphNodeKind::TextureSample2DArray,
        RenderMaterialGraphNodeKind::TextureObject2DArray,
        RenderMaterialGraphNodeKind::ParameterScalar,
        RenderMaterialGraphNodeKind::ParameterVector,
        RenderMaterialGraphNodeKind::ParameterColor,
        RenderMaterialGraphNodeKind::ParameterTexture,
        RenderMaterialGraphNodeKind::CollectionParameter,
        RenderMaterialGraphNodeKind::Add,
        RenderMaterialGraphNodeKind::Subtract,
        RenderMaterialGraphNodeKind::Multiply,
        RenderMaterialGraphNodeKind::Divide,
        RenderMaterialGraphNodeKind::Power,
        RenderMaterialGraphNodeKind::OneMinus,
        RenderMaterialGraphNodeKind::Clamp,
        RenderMaterialGraphNodeKind::Lerp,
        RenderMaterialGraphNodeKind::NormalUnpack,
        RenderMaterialGraphNodeKind::Uv,
        RenderMaterialGraphNodeKind::Absolute,
        RenderMaterialGraphNodeKind::Minimum,
        RenderMaterialGraphNodeKind::Maximum,
        RenderMaterialGraphNodeKind::Saturate,
        RenderMaterialGraphNodeKind::Floor,
        RenderMaterialGraphNodeKind::Ceil,
        RenderMaterialGraphNodeKind::Fraction,
        RenderMaterialGraphNodeKind::SquareRoot,
        RenderMaterialGraphNodeKind::Sine,
        RenderMaterialGraphNodeKind::Cosine,
        RenderMaterialGraphNodeKind::DotProduct,
        RenderMaterialGraphNodeKind::CrossProduct,
        RenderMaterialGraphNodeKind::Normalize,
        RenderMaterialGraphNodeKind::Length,
        RenderMaterialGraphNodeKind::Distance,
        RenderMaterialGraphNodeKind::BreakVector,
        RenderMaterialGraphNodeKind::MakeVector,
        RenderMaterialGraphNodeKind::Step,
        RenderMaterialGraphNodeKind::SmoothStep,
        RenderMaterialGraphNodeKind::If,
        RenderMaterialGraphNodeKind::RuntimeSwitch,
        RenderMaterialGraphNodeKind::Desaturate,
        RenderMaterialGraphNodeKind::Fresnel,
        RenderMaterialGraphNodeKind::Negate,
        RenderMaterialGraphNodeKind::Sign,
        RenderMaterialGraphNodeKind::Round,
        RenderMaterialGraphNodeKind::Truncate,
        RenderMaterialGraphNodeKind::Tangent,
        RenderMaterialGraphNodeKind::ArcSine,
        RenderMaterialGraphNodeKind::ArcCosine,
        RenderMaterialGraphNodeKind::ArcTangent,
        RenderMaterialGraphNodeKind::ArcTangent2,
        RenderMaterialGraphNodeKind::ArcSineFast,
        RenderMaterialGraphNodeKind::ArcCosineFast,
        RenderMaterialGraphNodeKind::ArcTangentFast,
        RenderMaterialGraphNodeKind::ArcTangent2Fast,
        RenderMaterialGraphNodeKind::ConstantVector2,
        RenderMaterialGraphNodeKind::ConstantBool,
        RenderMaterialGraphNodeKind::Time,
        RenderMaterialGraphNodeKind::DeltaTime,
        RenderMaterialGraphNodeKind::DynamicParameter,
        RenderMaterialGraphNodeKind::VertexColor,
        RenderMaterialGraphNodeKind::ScreenPosition,
        RenderMaterialGraphNodeKind::PixelPosition,
        RenderMaterialGraphNodeKind::LocalPosition,
        RenderMaterialGraphNodeKind::ObjectPosition,
        RenderMaterialGraphNodeKind::WorldPosition,
        RenderMaterialGraphNodeKind::PerInstanceRandom,
        RenderMaterialGraphNodeKind::PerInstanceFadeAmount,
        RenderMaterialGraphNodeKind::DistanceCullFade,
        RenderMaterialGraphNodeKind::PerInstanceCustomData,
        RenderMaterialGraphNodeKind::ObjectRadius,
        RenderMaterialGraphNodeKind::ObjectBounds,
        RenderMaterialGraphNodeKind::ObjectOrientation,
        RenderMaterialGraphNodeKind::PreSkinnedPosition,
        RenderMaterialGraphNodeKind::PreSkinnedNormal,
        RenderMaterialGraphNodeKind::MakeMaterialAttributes,
        RenderMaterialGraphNodeKind::BreakMaterialAttributes,
        RenderMaterialGraphNodeKind::BlendMaterialAttributes,
        RenderMaterialGraphNodeKind::GetMaterialAttributes,
        RenderMaterialGraphNodeKind::SetMaterialAttributes,
        RenderMaterialGraphNodeKind::StaticBoolParameter,
        RenderMaterialGraphNodeKind::StaticSwitch,
        RenderMaterialGraphNodeKind::StaticComponentMask,
        RenderMaterialGraphNodeKind::QualitySwitch,
        RenderMaterialGraphNodeKind::FeatureLevelSwitch,
        RenderMaterialGraphNodeKind::ShadingPathSwitch,
        RenderMaterialGraphNodeKind::ShaderStageSwitch,
        RenderMaterialGraphNodeKind::TextureCoordinate,
        RenderMaterialGraphNodeKind::Panner,
        RenderMaterialGraphNodeKind::Rotator,
        RenderMaterialGraphNodeKind::BumpOffset,
        RenderMaterialGraphNodeKind::ConstantBiasScale,
        RenderMaterialGraphNodeKind::RotateAboutAxis,
        RenderMaterialGraphNodeKind::ViewportUV,
        RenderMaterialGraphNodeKind::CameraPosition,
        RenderMaterialGraphNodeKind::CameraVector,
        RenderMaterialGraphNodeKind::ReflectionVector,
        RenderMaterialGraphNodeKind::LightVector,
        RenderMaterialGraphNodeKind::PixelNormalWS,
        RenderMaterialGraphNodeKind::VertexNormalWS,
        RenderMaterialGraphNodeKind::VertexTangentWS,
        RenderMaterialGraphNodeKind::ViewProperty,
        RenderMaterialGraphNodeKind::ViewSize,
        RenderMaterialGraphNodeKind::TwoSidedSign,
        RenderMaterialGraphNodeKind::SceneDepth,
        RenderMaterialGraphNodeKind::PixelDepth,
        RenderMaterialGraphNodeKind::CameraDepthFade,
        RenderMaterialGraphNodeKind::SceneColor,
        RenderMaterialGraphNodeKind::SceneTexture,
        RenderMaterialGraphNodeKind::DepthFade,
        RenderMaterialGraphNodeKind::Exponential,
        RenderMaterialGraphNodeKind::Exponential2,
        RenderMaterialGraphNodeKind::Logarithm,
        RenderMaterialGraphNodeKind::Logarithm2,
        RenderMaterialGraphNodeKind::SrgbToLinear,
        RenderMaterialGraphNodeKind::LinearToSrgb,
        RenderMaterialGraphNodeKind::Logarithm10,
        RenderMaterialGraphNodeKind::HsvToRgb,
        RenderMaterialGraphNodeKind::RgbToHsv,
        RenderMaterialGraphNodeKind::DeriveNormalZ,
        RenderMaterialGraphNodeKind::Fmod,
        RenderMaterialGraphNodeKind::InverseLerp,
        RenderMaterialGraphNodeKind::PartialDerivativeX,
        RenderMaterialGraphNodeKind::PartialDerivativeY,
        RenderMaterialGraphNodeKind::SphereMask,
        RenderMaterialGraphNodeKind::BlackBody,
        RenderMaterialGraphNodeKind::Noise,
        RenderMaterialGraphNodeKind::VectorNoise,
        RenderMaterialGraphNodeKind::Sobol,
        RenderMaterialGraphNodeKind::AppendVector,
        RenderMaterialGraphNodeKind::ColorRamp,
        RenderMaterialGraphNodeKind::AntialiasedTextureMask,
        RenderMaterialGraphNodeKind::Transform,
        RenderMaterialGraphNodeKind::TransformPosition,
        RenderMaterialGraphNodeKind::CustomCode,
        RenderMaterialGraphNodeKind::Reroute,
        RenderMaterialGraphNodeKind::NamedRerouteDeclaration,
        RenderMaterialGraphNodeKind::NamedRerouteUsage,
        RenderMaterialGraphNodeKind::CompositeInput,
        RenderMaterialGraphNodeKind::CompositeOutput,
        RenderMaterialGraphNodeKind::FunctionInput,
        RenderMaterialGraphNodeKind::FunctionOutput,
        RenderMaterialGraphNodeKind::MaterialFunctionCall,
        RenderMaterialGraphNodeKind::LayerStack,
    };
    return std::span<const RenderMaterialGraphNodeKind>{ kKinds };
}

std::string_view RenderMaterialGraphDiagnosticSeverityName(RenderMaterialGraphDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case RenderMaterialGraphDiagnosticSeverity::Error:
        return "Error";
    case RenderMaterialGraphDiagnosticSeverity::Warning:
        return "Warning";
    }
    return "Error";
}



} // namespace kb::render
