#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace kb::render {
namespace {

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

} // namespace

std::optional<RenderMaterialGraphNodeKind> ParseRenderMaterialGraphNodeKind(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "MaterialOutput")) {
        return RenderMaterialGraphNodeKind::MaterialOutput;
    }
    if (EqualsIgnoreCase(text, "ConstantScalar") || EqualsIgnoreCase(text, "Scalar")) {
        return RenderMaterialGraphNodeKind::ConstantScalar;
    }
    if (EqualsIgnoreCase(text, "ConstantVector2") || EqualsIgnoreCase(text, "Constant2Vector") || EqualsIgnoreCase(text, "Vector2") || EqualsIgnoreCase(text, "Float2") || EqualsIgnoreCase(text, "XY")) {
        return RenderMaterialGraphNodeKind::ConstantVector2;
    }
    if (EqualsIgnoreCase(text, "ConstantVector") || EqualsIgnoreCase(text, "Vector")) {
        return RenderMaterialGraphNodeKind::ConstantVector;
    }
    if (EqualsIgnoreCase(text, "ConstantColor") || EqualsIgnoreCase(text, "Color")) {
        return RenderMaterialGraphNodeKind::ConstantColor;
    }
    if (EqualsIgnoreCase(text, "ConstantBool") || EqualsIgnoreCase(text, "Bool") || EqualsIgnoreCase(text, "Boolean")) {
        return RenderMaterialGraphNodeKind::ConstantBool;
    }
    if (EqualsIgnoreCase(text, "TextureSample")) {
        return RenderMaterialGraphNodeKind::TextureSample;
    }
    if (EqualsIgnoreCase(text, "TextureObject") || EqualsIgnoreCase(text, "TextureObjectParameter")) {
        return RenderMaterialGraphNodeKind::TextureObject;
    }
    if (EqualsIgnoreCase(text, "TextureSampleCube") || EqualsIgnoreCase(text, "TextureCubeSample")) {
        return RenderMaterialGraphNodeKind::TextureSampleCube;
    }
    if (EqualsIgnoreCase(text, "TextureObjectCube") || EqualsIgnoreCase(text, "CubeTextureObject")) {
        return RenderMaterialGraphNodeKind::TextureObjectCube;
    }
    if (EqualsIgnoreCase(text, "TextureSampleVolume") || EqualsIgnoreCase(text, "VolumeTextureSample") || EqualsIgnoreCase(text, "TextureSample3D")) {
        return RenderMaterialGraphNodeKind::TextureSampleVolume;
    }
    if (EqualsIgnoreCase(text, "TextureObjectVolume") || EqualsIgnoreCase(text, "VolumeTextureObject") || EqualsIgnoreCase(text, "TextureObject3D")) {
        return RenderMaterialGraphNodeKind::TextureObjectVolume;
    }
    if (EqualsIgnoreCase(text, "TextureSample2DArray") || EqualsIgnoreCase(text, "Texture2DArraySample")) {
        return RenderMaterialGraphNodeKind::TextureSample2DArray;
    }
    if (EqualsIgnoreCase(text, "TextureObject2DArray") || EqualsIgnoreCase(text, "Texture2DArrayObject")) {
        return RenderMaterialGraphNodeKind::TextureObject2DArray;
    }
    if (EqualsIgnoreCase(text, "ParameterScalar")) {
        return RenderMaterialGraphNodeKind::ParameterScalar;
    }
    if (EqualsIgnoreCase(text, "ParameterVector")) {
        return RenderMaterialGraphNodeKind::ParameterVector;
    }
    if (EqualsIgnoreCase(text, "ParameterColor")) {
        return RenderMaterialGraphNodeKind::ParameterColor;
    }
    if (EqualsIgnoreCase(text, "ParameterTexture")) {
        return RenderMaterialGraphNodeKind::ParameterTexture;
    }
    if (EqualsIgnoreCase(text, "CollectionParameter") || EqualsIgnoreCase(text, "MaterialParameterCollection")) {
        return RenderMaterialGraphNodeKind::CollectionParameter;
    }
    if (EqualsIgnoreCase(text, "Add")) {
        return RenderMaterialGraphNodeKind::Add;
    }
    if (EqualsIgnoreCase(text, "Subtract")) {
        return RenderMaterialGraphNodeKind::Subtract;
    }
    if (EqualsIgnoreCase(text, "Multiply")) {
        return RenderMaterialGraphNodeKind::Multiply;
    }
    if (EqualsIgnoreCase(text, "Divide")) {
        return RenderMaterialGraphNodeKind::Divide;
    }
    if (EqualsIgnoreCase(text, "Power") || EqualsIgnoreCase(text, "Pow")) {
        return RenderMaterialGraphNodeKind::Power;
    }
    if (EqualsIgnoreCase(text, "OneMinus") || EqualsIgnoreCase(text, "OneMinusNode")) {
        return RenderMaterialGraphNodeKind::OneMinus;
    }
    if (EqualsIgnoreCase(text, "Absolute") || EqualsIgnoreCase(text, "Abs")) {
        return RenderMaterialGraphNodeKind::Absolute;
    }
    if (EqualsIgnoreCase(text, "Minimum") || EqualsIgnoreCase(text, "Min")) {
        return RenderMaterialGraphNodeKind::Minimum;
    }
    if (EqualsIgnoreCase(text, "Maximum") || EqualsIgnoreCase(text, "Max")) {
        return RenderMaterialGraphNodeKind::Maximum;
    }
    if (EqualsIgnoreCase(text, "Saturate")) {
        return RenderMaterialGraphNodeKind::Saturate;
    }
    if (EqualsIgnoreCase(text, "Floor")) {
        return RenderMaterialGraphNodeKind::Floor;
    }
    if (EqualsIgnoreCase(text, "Ceil") || EqualsIgnoreCase(text, "Ceiling")) {
        return RenderMaterialGraphNodeKind::Ceil;
    }
    if (EqualsIgnoreCase(text, "Fraction") || EqualsIgnoreCase(text, "Frac")) {
        return RenderMaterialGraphNodeKind::Fraction;
    }
    if (EqualsIgnoreCase(text, "SquareRoot") || EqualsIgnoreCase(text, "Sqrt")) {
        return RenderMaterialGraphNodeKind::SquareRoot;
    }
    if (EqualsIgnoreCase(text, "Sine") || EqualsIgnoreCase(text, "Sin")) {
        return RenderMaterialGraphNodeKind::Sine;
    }
    if (EqualsIgnoreCase(text, "Cosine") || EqualsIgnoreCase(text, "Cos")) {
        return RenderMaterialGraphNodeKind::Cosine;
    }
    if (EqualsIgnoreCase(text, "DotProduct") || EqualsIgnoreCase(text, "Dot")) {
        return RenderMaterialGraphNodeKind::DotProduct;
    }
    if (EqualsIgnoreCase(text, "CrossProduct") || EqualsIgnoreCase(text, "Cross")) {
        return RenderMaterialGraphNodeKind::CrossProduct;
    }
    if (EqualsIgnoreCase(text, "Normalize") || EqualsIgnoreCase(text, "NormalizeVector")) {
        return RenderMaterialGraphNodeKind::Normalize;
    }
    if (EqualsIgnoreCase(text, "Length")) {
        return RenderMaterialGraphNodeKind::Length;
    }
    if (EqualsIgnoreCase(text, "Distance")) {
        return RenderMaterialGraphNodeKind::Distance;
    }
    if (EqualsIgnoreCase(text, "BreakVector") || EqualsIgnoreCase(text, "BreakOutFloat4Components")) {
        return RenderMaterialGraphNodeKind::BreakVector;
    }
    if (EqualsIgnoreCase(text, "MakeVector") || EqualsIgnoreCase(text, "MakeFloat4")) {
        return RenderMaterialGraphNodeKind::MakeVector;
    }
    if (EqualsIgnoreCase(text, "AppendVector")) {
        return RenderMaterialGraphNodeKind::AppendVector;
    }
    if (EqualsIgnoreCase(text, "ColorRamp") || EqualsIgnoreCase(text, "Gradient")) {
        return RenderMaterialGraphNodeKind::ColorRamp;
    }
    if (EqualsIgnoreCase(text, "AntialiasedTextureMask") || EqualsIgnoreCase(text, "AAMask")) {
        return RenderMaterialGraphNodeKind::AntialiasedTextureMask;
    }
    if (EqualsIgnoreCase(text, "Transform") || EqualsIgnoreCase(text, "TransformVector")) {
        return RenderMaterialGraphNodeKind::Transform;
    }
    if (EqualsIgnoreCase(text, "TransformPosition")) {
        return RenderMaterialGraphNodeKind::TransformPosition;
    }
    if (EqualsIgnoreCase(text, "Step")) {
        return RenderMaterialGraphNodeKind::Step;
    }
    if (EqualsIgnoreCase(text, "SmoothStep") || EqualsIgnoreCase(text, "Smoothstep")) {
        return RenderMaterialGraphNodeKind::SmoothStep;
    }
    if (EqualsIgnoreCase(text, "If") || EqualsIgnoreCase(text, "Compare")) {
        return RenderMaterialGraphNodeKind::If;
    }
    if (EqualsIgnoreCase(text, "Switch") || EqualsIgnoreCase(text, "RuntimeSwitch") || EqualsIgnoreCase(text, "DynamicSwitch")) {
        return RenderMaterialGraphNodeKind::RuntimeSwitch;
    }
    if (EqualsIgnoreCase(text, "Desaturate") || EqualsIgnoreCase(text, "Desaturation")) {
        return RenderMaterialGraphNodeKind::Desaturate;
    }
    if (EqualsIgnoreCase(text, "Fresnel")) {
        return RenderMaterialGraphNodeKind::Fresnel;
    }
    if (EqualsIgnoreCase(text, "Negate") || EqualsIgnoreCase(text, "Minus")) {
        return RenderMaterialGraphNodeKind::Negate;
    }
    if (EqualsIgnoreCase(text, "Sign")) {
        return RenderMaterialGraphNodeKind::Sign;
    }
    if (EqualsIgnoreCase(text, "Round")) {
        return RenderMaterialGraphNodeKind::Round;
    }
    if (EqualsIgnoreCase(text, "Truncate") || EqualsIgnoreCase(text, "Trunc")) {
        return RenderMaterialGraphNodeKind::Truncate;
    }
    if (EqualsIgnoreCase(text, "Tangent") || EqualsIgnoreCase(text, "Tan")) {
        return RenderMaterialGraphNodeKind::Tangent;
    }
    if (EqualsIgnoreCase(text, "ArcSine") || EqualsIgnoreCase(text, "Arcsine") || EqualsIgnoreCase(text, "Asin")) {
        return RenderMaterialGraphNodeKind::ArcSine;
    }
    if (EqualsIgnoreCase(text, "ArcCosine") || EqualsIgnoreCase(text, "Arccosine") || EqualsIgnoreCase(text, "Acos")) {
        return RenderMaterialGraphNodeKind::ArcCosine;
    }
    if (EqualsIgnoreCase(text, "ArcTangent") || EqualsIgnoreCase(text, "Arctangent") || EqualsIgnoreCase(text, "Atan")) {
        return RenderMaterialGraphNodeKind::ArcTangent;
    }
    if (EqualsIgnoreCase(text, "ArcTangent2") || EqualsIgnoreCase(text, "Arctangent2") || EqualsIgnoreCase(text, "Atan2")) {
        return RenderMaterialGraphNodeKind::ArcTangent2;
    }
    if (EqualsIgnoreCase(text, "ArcSineFast") || EqualsIgnoreCase(text, "ArcsineFast") || EqualsIgnoreCase(text, "AsinFast")) {
        return RenderMaterialGraphNodeKind::ArcSineFast;
    }
    if (EqualsIgnoreCase(text, "ArcCosineFast") || EqualsIgnoreCase(text, "ArccosineFast") || EqualsIgnoreCase(text, "AcosFast")) {
        return RenderMaterialGraphNodeKind::ArcCosineFast;
    }
    if (EqualsIgnoreCase(text, "ArcTangentFast") || EqualsIgnoreCase(text, "ArctangentFast") || EqualsIgnoreCase(text, "AtanFast")) {
        return RenderMaterialGraphNodeKind::ArcTangentFast;
    }
    if (EqualsIgnoreCase(text, "ArcTangent2Fast") || EqualsIgnoreCase(text, "Arctangent2Fast") || EqualsIgnoreCase(text, "Atan2Fast")) {
        return RenderMaterialGraphNodeKind::ArcTangent2Fast;
    }
    if (EqualsIgnoreCase(text, "Clamp")) {
        return RenderMaterialGraphNodeKind::Clamp;
    }
    if (EqualsIgnoreCase(text, "Lerp")) {
        return RenderMaterialGraphNodeKind::Lerp;
    }
    if (EqualsIgnoreCase(text, "NormalUnpack")) {
        return RenderMaterialGraphNodeKind::NormalUnpack;
    }
    if (EqualsIgnoreCase(text, "UV") || EqualsIgnoreCase(text, "Uv")) {
        return RenderMaterialGraphNodeKind::Uv;
    }
    if (EqualsIgnoreCase(text, "Time")) {
        return RenderMaterialGraphNodeKind::Time;
    }
    if (EqualsIgnoreCase(text, "DeltaTime") || EqualsIgnoreCase(text, "TimeDelta")) {
        return RenderMaterialGraphNodeKind::DeltaTime;
    }
    if (EqualsIgnoreCase(text, "DynamicParameter") || EqualsIgnoreCase(text, "DynamicParameters")) {
        return RenderMaterialGraphNodeKind::DynamicParameter;
    }
    if (EqualsIgnoreCase(text, "VertexColor")) {
        return RenderMaterialGraphNodeKind::VertexColor;
    }
    if (EqualsIgnoreCase(text, "ScreenPosition")) {
        return RenderMaterialGraphNodeKind::ScreenPosition;
    }
    if (EqualsIgnoreCase(text, "PixelPosition") || EqualsIgnoreCase(text, "ViewportPixelPosition") || EqualsIgnoreCase(text, "ScreenPixelPosition")) {
        return RenderMaterialGraphNodeKind::PixelPosition;
    }
    if (EqualsIgnoreCase(text, "LocalPosition")) {
        return RenderMaterialGraphNodeKind::LocalPosition;
    }
    if (EqualsIgnoreCase(text, "ObjectPosition")) {
        return RenderMaterialGraphNodeKind::ObjectPosition;
    }
    if (EqualsIgnoreCase(text, "WorldPosition")) {
        return RenderMaterialGraphNodeKind::WorldPosition;
    }
    if (EqualsIgnoreCase(text, "PerInstanceRandom")) {
        return RenderMaterialGraphNodeKind::PerInstanceRandom;
    }
    if (EqualsIgnoreCase(text, "PerInstanceFadeAmount") || EqualsIgnoreCase(text, "PerInstanceFade")) {
        return RenderMaterialGraphNodeKind::PerInstanceFadeAmount;
    }
    if (EqualsIgnoreCase(text, "DistanceCullFade") || EqualsIgnoreCase(text, "CullDistanceFade")) {
        return RenderMaterialGraphNodeKind::DistanceCullFade;
    }
    if (EqualsIgnoreCase(text, "PerInstanceCustomData") || EqualsIgnoreCase(text, "PerInstanceCustomData0")) {
        return RenderMaterialGraphNodeKind::PerInstanceCustomData;
    }
    if (EqualsIgnoreCase(text, "ObjectRadius")) {
        return RenderMaterialGraphNodeKind::ObjectRadius;
    }
    if (EqualsIgnoreCase(text, "ObjectBounds")) {
        return RenderMaterialGraphNodeKind::ObjectBounds;
    }
    if (EqualsIgnoreCase(text, "ObjectOrientation")) {
        return RenderMaterialGraphNodeKind::ObjectOrientation;
    }
    if (EqualsIgnoreCase(text, "PreSkinnedPosition") || EqualsIgnoreCase(text, "PreSkinnedLocalPosition")) {
        return RenderMaterialGraphNodeKind::PreSkinnedPosition;
    }
    if (EqualsIgnoreCase(text, "PreSkinnedNormal") || EqualsIgnoreCase(text, "PreSkinnedLocalNormal")) {
        return RenderMaterialGraphNodeKind::PreSkinnedNormal;
    }
    if (EqualsIgnoreCase(text, "MakeMaterialAttributes")) {
        return RenderMaterialGraphNodeKind::MakeMaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "BreakMaterialAttributes")) {
        return RenderMaterialGraphNodeKind::BreakMaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "BlendMaterialAttributes")) {
        return RenderMaterialGraphNodeKind::BlendMaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "GetMaterialAttributes")) {
        return RenderMaterialGraphNodeKind::GetMaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "SetMaterialAttributes")) {
        return RenderMaterialGraphNodeKind::SetMaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "StaticBoolParameter")) {
        return RenderMaterialGraphNodeKind::StaticBoolParameter;
    }
    if (EqualsIgnoreCase(text, "StaticSwitch") || EqualsIgnoreCase(text, "StaticSwitchParameter")) {
        return RenderMaterialGraphNodeKind::StaticSwitch;
    }
    if (EqualsIgnoreCase(text, "StaticComponentMask") || EqualsIgnoreCase(text, "StaticComponentMaskParameter") ||
        EqualsIgnoreCase(text, "ComponentMask") || EqualsIgnoreCase(text, "ChannelMask") ||
        EqualsIgnoreCase(text, "ChannelMaskParameter") || EqualsIgnoreCase(text, "Mask")) {
        return RenderMaterialGraphNodeKind::StaticComponentMask;
    }
    if (EqualsIgnoreCase(text, "QualitySwitch") || EqualsIgnoreCase(text, "MaterialQualitySwitch")) {
        return RenderMaterialGraphNodeKind::QualitySwitch;
    }
    if (EqualsIgnoreCase(text, "FeatureLevelSwitch")) {
        return RenderMaterialGraphNodeKind::FeatureLevelSwitch;
    }
    if (EqualsIgnoreCase(text, "ShadingPathSwitch") || EqualsIgnoreCase(text, "RenderPathSwitch")) {
        return RenderMaterialGraphNodeKind::ShadingPathSwitch;
    }
    if (EqualsIgnoreCase(text, "ShaderStageSwitch") || EqualsIgnoreCase(text, "StageSwitch")) {
        return RenderMaterialGraphNodeKind::ShaderStageSwitch;
    }
    if (EqualsIgnoreCase(text, "TextureCoordinate")) {
        return RenderMaterialGraphNodeKind::TextureCoordinate;
    }
    if (EqualsIgnoreCase(text, "Panner")) {
        return RenderMaterialGraphNodeKind::Panner;
    }
    if (EqualsIgnoreCase(text, "Rotator")) {
        return RenderMaterialGraphNodeKind::Rotator;
    }
    if (EqualsIgnoreCase(text, "BumpOffset")) {
        return RenderMaterialGraphNodeKind::BumpOffset;
    }
    if (EqualsIgnoreCase(text, "ConstantBiasScale")) {
        return RenderMaterialGraphNodeKind::ConstantBiasScale;
    }
    if (EqualsIgnoreCase(text, "RotateAboutAxis")) {
        return RenderMaterialGraphNodeKind::RotateAboutAxis;
    }
    if (EqualsIgnoreCase(text, "ViewportUV")) {
        return RenderMaterialGraphNodeKind::ViewportUV;
    }
    if (EqualsIgnoreCase(text, "CameraPosition")) {
        return RenderMaterialGraphNodeKind::CameraPosition;
    }
    if (EqualsIgnoreCase(text, "CameraVector")) {
        return RenderMaterialGraphNodeKind::CameraVector;
    }
    if (EqualsIgnoreCase(text, "ReflectionVector")) {
        return RenderMaterialGraphNodeKind::ReflectionVector;
    }
    if (EqualsIgnoreCase(text, "LightVector")) {
        return RenderMaterialGraphNodeKind::LightVector;
    }
    if (EqualsIgnoreCase(text, "PixelNormalWS")) {
        return RenderMaterialGraphNodeKind::PixelNormalWS;
    }
    if (EqualsIgnoreCase(text, "VertexNormalWS")) {
        return RenderMaterialGraphNodeKind::VertexNormalWS;
    }
    if (EqualsIgnoreCase(text, "VertexTangentWS")) {
        return RenderMaterialGraphNodeKind::VertexTangentWS;
    }
    if (EqualsIgnoreCase(text, "ViewProperty")) {
        return RenderMaterialGraphNodeKind::ViewProperty;
    }
    if (EqualsIgnoreCase(text, "ViewSize")) {
        return RenderMaterialGraphNodeKind::ViewSize;
    }
    if (EqualsIgnoreCase(text, "TwoSidedSign")) {
        return RenderMaterialGraphNodeKind::TwoSidedSign;
    }
    if (EqualsIgnoreCase(text, "SceneDepth")) {
        return RenderMaterialGraphNodeKind::SceneDepth;
    }
    if (EqualsIgnoreCase(text, "PixelDepth")) {
        return RenderMaterialGraphNodeKind::PixelDepth;
    }
    if (EqualsIgnoreCase(text, "CameraDepthFade") || EqualsIgnoreCase(text, "CameraFade") || EqualsIgnoreCase(text, "DistanceFade")) {
        return RenderMaterialGraphNodeKind::CameraDepthFade;
    }
    if (EqualsIgnoreCase(text, "SceneColor")) {
        return RenderMaterialGraphNodeKind::SceneColor;
    }
    if (EqualsIgnoreCase(text, "SceneTexture")) {
        return RenderMaterialGraphNodeKind::SceneTexture;
    }
    if (EqualsIgnoreCase(text, "DepthFade")) {
        return RenderMaterialGraphNodeKind::DepthFade;
    }
    if (EqualsIgnoreCase(text, "CustomCode") || EqualsIgnoreCase(text, "CustomHLSL") || EqualsIgnoreCase(text, "CustomShaderCode")) {
        return RenderMaterialGraphNodeKind::CustomCode;
    }
    if (EqualsIgnoreCase(text, "Reroute") || EqualsIgnoreCase(text, "RerouteNode")) {
        return RenderMaterialGraphNodeKind::Reroute;
    }
    if (EqualsIgnoreCase(text, "NamedRerouteDeclaration") || EqualsIgnoreCase(text, "NamedRerouteDecl") || EqualsIgnoreCase(text, "NamedReroute")) {
        return RenderMaterialGraphNodeKind::NamedRerouteDeclaration;
    }
    if (EqualsIgnoreCase(text, "NamedRerouteUsage") || EqualsIgnoreCase(text, "NamedRerouteUse")) {
        return RenderMaterialGraphNodeKind::NamedRerouteUsage;
    }
    if (EqualsIgnoreCase(text, "CompositeInput") || EqualsIgnoreCase(text, "SubgraphInput") || EqualsIgnoreCase(text, "TunnelInput")) {
        return RenderMaterialGraphNodeKind::CompositeInput;
    }
    if (EqualsIgnoreCase(text, "CompositeOutput") || EqualsIgnoreCase(text, "SubgraphOutput") || EqualsIgnoreCase(text, "TunnelOutput")) {
        return RenderMaterialGraphNodeKind::CompositeOutput;
    }
    if (EqualsIgnoreCase(text, "FunctionInput") || EqualsIgnoreCase(text, "MaterialFunctionInput")) {
        return RenderMaterialGraphNodeKind::FunctionInput;
    }
    if (EqualsIgnoreCase(text, "FunctionOutput") || EqualsIgnoreCase(text, "MaterialFunctionOutput")) {
        return RenderMaterialGraphNodeKind::FunctionOutput;
    }
    if (EqualsIgnoreCase(text, "MaterialFunctionCall") || EqualsIgnoreCase(text, "FunctionCall")) {
        return RenderMaterialGraphNodeKind::MaterialFunctionCall;
    }
    if (EqualsIgnoreCase(text, "LayerStack") || EqualsIgnoreCase(text, "MaterialLayerStack")) {
        return RenderMaterialGraphNodeKind::LayerStack;
    }
    if (EqualsIgnoreCase(text, "Exponential") || EqualsIgnoreCase(text, "Exp")) {
        return RenderMaterialGraphNodeKind::Exponential;
    }
    if (EqualsIgnoreCase(text, "Exponential2") || EqualsIgnoreCase(text, "Exp2")) {
        return RenderMaterialGraphNodeKind::Exponential2;
    }
    if (EqualsIgnoreCase(text, "Logarithm") || EqualsIgnoreCase(text, "Log")) {
        return RenderMaterialGraphNodeKind::Logarithm;
    }
    if (EqualsIgnoreCase(text, "Logarithm2") || EqualsIgnoreCase(text, "Log2")) {
        return RenderMaterialGraphNodeKind::Logarithm2;
    }
    if (EqualsIgnoreCase(text, "SrgbToLinear")) {
        return RenderMaterialGraphNodeKind::SrgbToLinear;
    }
    if (EqualsIgnoreCase(text, "LinearToSrgb")) {
        return RenderMaterialGraphNodeKind::LinearToSrgb;
    }
    if (EqualsIgnoreCase(text, "Logarithm10") || EqualsIgnoreCase(text, "Log10")) {
        return RenderMaterialGraphNodeKind::Logarithm10;
    }
    if (EqualsIgnoreCase(text, "HsvToRgb")) {
        return RenderMaterialGraphNodeKind::HsvToRgb;
    }
    if (EqualsIgnoreCase(text, "RgbToHsv")) {
        return RenderMaterialGraphNodeKind::RgbToHsv;
    }
    if (EqualsIgnoreCase(text, "DeriveNormalZ")) {
        return RenderMaterialGraphNodeKind::DeriveNormalZ;
    }
    if (EqualsIgnoreCase(text, "Fmod") || EqualsIgnoreCase(text, "Modulo")) {
        return RenderMaterialGraphNodeKind::Fmod;
    }
    if (EqualsIgnoreCase(text, "InverseLerp")) {
        return RenderMaterialGraphNodeKind::InverseLerp;
    }
    if (EqualsIgnoreCase(text, "PartialDerivativeX") || EqualsIgnoreCase(text, "DDX")) {
        return RenderMaterialGraphNodeKind::PartialDerivativeX;
    }
    if (EqualsIgnoreCase(text, "PartialDerivativeY") || EqualsIgnoreCase(text, "DDY")) {
        return RenderMaterialGraphNodeKind::PartialDerivativeY;
    }
    if (EqualsIgnoreCase(text, "SphereMask")) {
        return RenderMaterialGraphNodeKind::SphereMask;
    }
    if (EqualsIgnoreCase(text, "BlackBody") || EqualsIgnoreCase(text, "BlackbodyRadiation")) {
        return RenderMaterialGraphNodeKind::BlackBody;
    }
    if (EqualsIgnoreCase(text, "Noise")) {
        return RenderMaterialGraphNodeKind::Noise;
    }
    if (EqualsIgnoreCase(text, "VectorNoise")) {
        return RenderMaterialGraphNodeKind::VectorNoise;
    }
    if (EqualsIgnoreCase(text, "Sobol") || EqualsIgnoreCase(text, "Sobol2D") || EqualsIgnoreCase(text, "LowDiscrepancy")) {
        return RenderMaterialGraphNodeKind::Sobol;
    }
    return std::nullopt;
}


} // namespace kb::render
