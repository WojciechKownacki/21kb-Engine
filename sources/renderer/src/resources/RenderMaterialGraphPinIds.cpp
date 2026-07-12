#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <string_view>

namespace kb::render {
namespace {

[[nodiscard]] constexpr std::uint32_t PinId(std::uint16_t nodeKind, std::uint8_t direction, std::uint16_t ordinal) noexcept {
    return (static_cast<std::uint32_t>(nodeKind) << 16U) |
        (static_cast<std::uint32_t>(direction) << 12U) |
        static_cast<std::uint32_t>(ordinal);
}

void HashUInt32(std::uint32_t& hash, std::uint32_t value) noexcept {
    for (int index = 0; index < 4; ++index) {
        hash ^= static_cast<std::uint8_t>((value >> static_cast<unsigned>(index * 8)) & 0xFFU);
        hash *= 16777619U;
    }
}

} // namespace

std::uint32_t RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept {
    const std::uint16_t nodeKind = static_cast<std::uint16_t>(kind) + 1U;
    const std::uint8_t direction = outputPin ? 2U : 1U;
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        if (!outputPin && pin == "baseColor") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "metallic") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "roughness") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "normal") return PinId(nodeKind, direction, 4U);
        if (!outputPin && pin == "emissive") return PinId(nodeKind, direction, 5U);
        if (!outputPin && pin == "occlusion") return PinId(nodeKind, direction, 6U);
        if (!outputPin && pin == "alpha") return PinId(nodeKind, direction, 7U);
        if (!outputPin && pin == "alphaClipThreshold") return PinId(nodeKind, direction, 8U);
        if (!outputPin && pin == "worldPositionOffset") return PinId(nodeKind, direction, 9U);
        if (!outputPin && pin == "specular") return PinId(nodeKind, direction, 10U);
        if (!outputPin && pin == "attributes") return PinId(nodeKind, direction, 18U);
        if (!outputPin && pin == "customizedUv0") return PinId(nodeKind, direction, 19U);
        if (!outputPin && pin == "displacement") return PinId(nodeKind, direction, 20U);
        if (!outputPin && pin == "tangentOutput") return PinId(nodeKind, direction, 23U);
        return 0U;
    case RenderMaterialGraphNodeKind::TextureSample:
        if (!outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "uv") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        if (!outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "direction") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        if (!outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "uvw") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        if (!outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "uv") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "layer") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
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
        if (!outputPin && pin == "a") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "b") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::InverseLerp:
        if (!outputPin && pin == "a") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "b") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Power:
        if (!outputPin && pin == "base") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "exponent") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::CustomCode:
        if (!outputPin && pin == "A") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "B") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
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
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "x") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "y") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "z") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "w") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::MakeVector:
        if (!outputPin && pin == "x") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "y") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "z") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "w") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Step:
        if (!outputPin && pin == "edge") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::SmoothStep:
        if (!outputPin && pin == "min") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "max") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::If:
        if (!outputPin && pin == "a") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "b") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "less") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "equal") return PinId(nodeKind, direction, 4U);
        if (!outputPin && pin == "greater") return PinId(nodeKind, direction, 5U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        if (!outputPin && pin == "index") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "default") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "case0") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "case1") return PinId(nodeKind, direction, 4U);
        if (!outputPin && pin == "case2") return PinId(nodeKind, direction, 5U);
        if (!outputPin && pin == "case3") return PinId(nodeKind, direction, 6U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Sobol:
        if (!outputPin && pin == "cell") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "index") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "seed") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Desaturate:
        if (!outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "fraction") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Fresnel:
        if (!outputPin && pin == "normal") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "view") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "exponent") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "base") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
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
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
        if (!outputPin && pin == "y") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "x") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Clamp:
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "min") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "max") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Lerp:
        if (!outputPin && pin == "a") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "b") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "t") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        if (!outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "normal") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        if (outputPin && pin == "xy") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantVector:
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        return 0U;
    case RenderMaterialGraphNodeKind::ParameterVector:
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "scalar") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 5U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 6U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 7U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 8U);
        return 0U;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        if (outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Uv:
        if (outputPin && pin == "uv") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::DeltaTime:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
    case RenderMaterialGraphNodeKind::DistanceCullFade:
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::ObjectBounds:
    case RenderMaterialGraphNodeKind::ObjectOrientation:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::DynamicParameter:
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        if (outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        if (pin == "baseColor") return PinId(nodeKind, direction, 2U);
        if (pin == "metallic") return PinId(nodeKind, direction, 3U);
        if (pin == "roughness") return PinId(nodeKind, direction, 4U);
        if (pin == "normal") return PinId(nodeKind, direction, 5U);
        if (pin == "emissive") return PinId(nodeKind, direction, 6U);
        if (pin == "occlusion") return PinId(nodeKind, direction, 7U);
        if (pin == "alpha") return PinId(nodeKind, direction, 8U);
        if (pin == "alphaClipThreshold") return PinId(nodeKind, direction, 9U);
        if (pin == "specular") return PinId(nodeKind, direction, 10U);
        if (pin == "tangentOutput") return PinId(nodeKind, direction, 20U);
        return 0U;
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        if (outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        if (pin == "a") return PinId(nodeKind, direction, 2U);
        if (pin == "b") return PinId(nodeKind, direction, 3U);
        if (pin == "factor") return PinId(nodeKind, direction, 4U);
        return 0U;
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        if (!outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        if (pin == "baseColor") return PinId(nodeKind, direction, 2U);
        if (pin == "metallic") return PinId(nodeKind, direction, 3U);
        if (pin == "roughness") return PinId(nodeKind, direction, 4U);
        if (pin == "normal") return PinId(nodeKind, direction, 5U);
        if (pin == "emissive") return PinId(nodeKind, direction, 6U);
        if (pin == "occlusion") return PinId(nodeKind, direction, 7U);
        if (pin == "alpha") return PinId(nodeKind, direction, 8U);
        if (pin == "alphaClipThreshold") return PinId(nodeKind, direction, 9U);
        if (pin == "specular") return PinId(nodeKind, direction, 10U);
        if (pin == "tangentOutput") return PinId(nodeKind, direction, 20U);
        return 0U;
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        if (!outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        if (pin == "baseColor") return PinId(nodeKind, direction, 2U);
        if (pin == "metallic") return PinId(nodeKind, direction, 3U);
        if (pin == "roughness") return PinId(nodeKind, direction, 4U);
        if (pin == "normal") return PinId(nodeKind, direction, 5U);
        if (pin == "emissive") return PinId(nodeKind, direction, 6U);
        if (pin == "occlusion") return PinId(nodeKind, direction, 7U);
        if (pin == "alpha") return PinId(nodeKind, direction, 8U);
        if (outputPin && pin == "attributesOut") return PinId(nodeKind, direction, 9U);
        if (pin == "alphaClipThreshold") return PinId(nodeKind, direction, 9U);
        if (pin == "specular") return PinId(nodeKind, direction, 10U);
        if (pin == "tangentOutput") return PinId(nodeKind, direction, 20U);
        return 0U;
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::StaticSwitch:
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "true") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "false") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 4U);
        return 0U;
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        if (!outputPin && pin == "input") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 2U);
        return 0U;
    case RenderMaterialGraphNodeKind::QualitySwitch:
        if (!outputPin && pin == "low") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "med") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "high") return PinId(nodeKind, direction, 3U);
        if (!outputPin && pin == "epic") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        if (!outputPin && pin == "es3") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "sm5") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "sm6") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 4U);
        return 0U;
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        if (!outputPin && pin == "forward") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "forwardPlus") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "deferred") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 4U);
        return 0U;
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        if (!outputPin && pin == "vertex") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "fragment") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 3U);
        return 0U;
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::ViewportUV:
        if (outputPin && pin == "uv") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
        if (!outputPin && pin == "coordinate") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "time") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "uv") return PinId(nodeKind, direction, 3U);
        return 0U;
    case RenderMaterialGraphNodeKind::BumpOffset:
        if (!outputPin && pin == "coordinate") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "height") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "uv") return PinId(nodeKind, direction, 3U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        if (!outputPin && pin == "input") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 2U);
        return 0U;
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        if (!outputPin && pin == "axis") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "angle") return PinId(nodeKind, direction, 2U);
        if (!outputPin && pin == "position") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "result") return PinId(nodeKind, direction, 4U);
        return 0U;
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
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::CameraDepthFade:
        if (!outputPin && pin == "fadeLength") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "fadeOffset") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 3U);
        return 0U;
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        if (!outputPin && pin == "uv") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "color") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::DepthFade:
        if (!outputPin && pin == "fadeDistance") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 2U);
        return 0U;
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        if (!outputPin && pin == "input") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "output") return PinId(nodeKind, direction, 2U);
        return 0U;
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        if (!outputPin && pin == "input") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        if (outputPin && pin == "output") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::FunctionInput:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::FunctionOutput:
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return 0U;
    case RenderMaterialGraphNodeKind::LayerStack:
        if (outputPin && pin == "attributes") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::VertexColor:
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::PixelPosition:
        if (outputPin && pin == "xy") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        return 0U;
    }
    return 0U;
}

std::uint32_t RenderMaterialGraphStablePinId(const RenderMaterialGraphNode& node, std::string_view pin, bool outputPin) noexcept {
    if (node.kind == RenderMaterialGraphNodeKind::FunctionInput) {
        return (outputPin && pin == "value")
            ? PinId(static_cast<std::uint16_t>(node.kind) + 1U, 2U, 1U)
            : 0U;
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
        return (!outputPin && pin == "value")
            ? PinId(static_cast<std::uint16_t>(node.kind) + 1U, 1U, 1U)
            : 0U;
    }
    if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        const std::vector<RenderMaterialGraphCustomPin>& pins = outputPin ? node.customCode.outputs : node.customCode.inputs;
        for (std::size_t index = 0U; index < pins.size(); ++index) {
            if (pins[index].name == pin) {
                return PinId(
                    static_cast<std::uint16_t>(node.kind) + 1U,
                    outputPin ? 2U : 1U,
                    static_cast<std::uint16_t>(index + 1U));
            }
        }
        return 0U;
    }
    if (node.kind != RenderMaterialGraphNodeKind::CustomCode) {
        return RenderMaterialGraphStablePinId(node.kind, pin, outputPin);
    }
    const std::uint16_t nodeKind = static_cast<std::uint16_t>(node.kind) + 1U;
    const std::uint8_t direction = outputPin ? 2U : 1U;
    if (outputPin) {
        if (pin == "value") {
            return PinId(nodeKind, direction, 1U);
        }
        for (std::size_t index = 0U; index < node.customCode.outputs.size(); ++index) {
            if (node.customCode.outputs[index].name == pin) {
                return PinId(nodeKind, direction, static_cast<std::uint16_t>(index + 2U));
            }
        }
        return 0U;
    }
    for (std::size_t index = 0U; index < node.customCode.inputs.size(); ++index) {
        if (node.customCode.inputs[index].name == pin) {
            return PinId(nodeKind, direction, static_cast<std::uint16_t>(index + 1U));
        }
    }
    return 0U;
}


std::uint32_t MakeRenderMaterialGraphLinkId(const RenderMaterialGraphLink& link) noexcept {
    std::uint32_t hash = 2166136261U;
    HashUInt32(hash, link.fromNodeId);
    HashUInt32(hash, link.fromPinId);
    HashUInt32(hash, link.toNodeId);
    HashUInt32(hash, link.toPinId);
    if (hash == 0U) {
        return 1U;
    }
    return hash;
}

bool IsRenderMaterialGraphParameterNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return true;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return false;
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::TextureSample:
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
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        return false;
    }
    return false;
}


} // namespace kb::render
