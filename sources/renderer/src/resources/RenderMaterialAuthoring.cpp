#include "kb/render/resources/RenderMaterialAuthoring.hpp"

#include "kb/render/resources/RenderMaterialNumericParsing.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>
#include <vector>

namespace kb::render {

RenderMaterialGraphCustomCode DefaultRenderMaterialGraphCustomCode() {
    return RenderMaterialGraphCustomCode{
        .body = "return A * B;",
        .outputType = RenderMaterialGraphPinType::Float4,
        .inputs = {
            RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::Float4 },
            RenderMaterialGraphCustomPin{ .name = "B", .type = RenderMaterialGraphPinType::Float4 },
        },
        .outputs = {},
    };
}

RenderMaterialGraphCustomCode DefaultRenderMaterialGraphFunctionCall() {
    return RenderMaterialGraphCustomCode{
        .body = {},
        .outputType = RenderMaterialGraphPinType::Float4,
        .inputs = {
            RenderMaterialGraphCustomPin{ .name = "Input", .type = RenderMaterialGraphPinType::Float4 },
        },
        .outputs = {
            RenderMaterialGraphCustomPin{ .name = "Output", .type = RenderMaterialGraphPinType::Float4 },
        },
    };
}

RenderMaterialGraphParameterMetadata DefaultRenderMaterialGraphParameterMetadata(
    RenderMaterialGraphNodeKind kind,
    std::uint32_t nodeId
) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "scalar" + std::to_string(nodeId),
            .displayName = "Scalar " + std::to_string(nodeId),
            .defaultValueHint = "0",
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "vector" + std::to_string(nodeId),
            .displayName = "Vector " + std::to_string(nodeId),
            .defaultValueHint = "0 0 0",
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "color" + std::to_string(nodeId),
            .displayName = "Color " + std::to_string(nodeId),
            .defaultValueHint = "1 1 1 1",
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "texture" + std::to_string(nodeId),
            .displayName = "Texture " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureObject" + std::to_string(nodeId),
            .displayName = "Texture Object " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureCubeObject" + std::to_string(nodeId),
            .displayName = "Texture Cube Object " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureVolumeObject" + std::to_string(nodeId),
            .displayName = "Texture Volume Object " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureArrayObject" + std::to_string(nodeId),
            .displayName = "Texture 2D Array Object " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "collectionParam" + std::to_string(nodeId),
            .displayName = "Collection Parameter " + std::to_string(nodeId),
            .defaultValueHint = "0",
            .overrideSupported = false,
            .description = "Material parameter collection value",
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample" + std::to_string(nodeId),
            .displayName = "Texture Sample " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureCubeSample" + std::to_string(nodeId),
            .displayName = "Texture Cube Sample " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureVolumeSample" + std::to_string(nodeId),
            .displayName = "Texture Volume Sample " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "textureArraySample" + std::to_string(nodeId),
            .displayName = "Texture 2D Array Sample " + std::to_string(nodeId),
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Scalar",
            .defaultValueHint = "0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Bool",
            .defaultValueHint = "false",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "XY",
            .defaultValueHint = "0 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "RGB Node",
            .defaultValueHint = "0 0 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "RGBA Node",
            .defaultValueHint = "1 1 1 1",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::CustomCode:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Custom Code",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Switch",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::StaticSwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Static Switch",
            .defaultValueHint = "false",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::StaticComponentMask:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Static Component Mask",
            .defaultValueHint = "rgba",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::QualitySwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Quality Switch",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Feature Level Switch",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Shading Path Switch",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Shader Stage Switch",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Reroute",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "NamedReroute",
            .displayName = "Named Reroute",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "NamedReroute",
            .displayName = "Named Reroute",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Composite Input",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Composite Output",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "Input",
            .displayName = "Function Input",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "Output",
            .displayName = "Function Output",
            .defaultValueHint = "float4",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = {},
            .displayName = "Material Function",
            .defaultValueHint = {},
            .overrideSupported = false,
            .description = "Assign a RenderMaterialFunction asset id to call a reusable material function.",
        };
    case kb::render::RenderMaterialGraphNodeKind::LayerStack:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "surfaceLayers",
            .displayName = "Layer Stack",
            .defaultValueHint = {},
            .overrideSupported = false,
            .description = "Composes material layer functions with material blend functions.",
        };
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "UV",
            .defaultValueHint = "0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureCoordinate:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Texture Coordinate",
            .defaultValueHint = "1 1 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Panner:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Panner",
            .defaultValueHint = "0 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Rotator:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Rotator",
            .defaultValueHint = "0.25 0.5 0.5",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::BumpOffset:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Bump Offset",
            .defaultValueHint = "0.05",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ConstantBiasScale:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Constant Bias Scale",
            .defaultValueHint = "0 1",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::RotateAboutAxis:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Rotate About Axis",
            .defaultValueHint = "0 0 1 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Desaturate",
            .defaultValueHint = "1",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Fresnel",
            .defaultValueHint = "5 0.04",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Sphere Mask",
            .defaultValueHint = "1 0.5",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Antialiased Texture Mask",
            .defaultValueHint = "0.5",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::Transform:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Transform",
            .defaultValueHint = "tangent world",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Transform Position",
            .defaultValueHint = "tangent world",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ViewProperty:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "View Property",
            .defaultValueHint = "viewSize",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::SceneTexture:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Scene Texture",
            .defaultValueHint = "color",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::CameraDepthFade:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Camera Depth Fade",
            .defaultValueHint = "1 0",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::DepthFade:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Depth Fade",
            .defaultValueHint = "0.01",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
        return kb::render::RenderMaterialGraphParameterMetadata{
            .displayName = "Color Ramp",
            .defaultValueHint = "0 0 0 0 1 1 1 1",
            .overrideSupported = false,
        };
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Power:
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::Saturate:
    case kb::render::RenderMaterialGraphNodeKind::Floor:
    case kb::render::RenderMaterialGraphNodeKind::Ceil:
    case kb::render::RenderMaterialGraphNodeKind::Fraction:
    case kb::render::RenderMaterialGraphNodeKind::SquareRoot:
    case kb::render::RenderMaterialGraphNodeKind::Sine:
    case kb::render::RenderMaterialGraphNodeKind::Cosine:
    case kb::render::RenderMaterialGraphNodeKind::Exponential:
    case kb::render::RenderMaterialGraphNodeKind::Exponential2:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm2:
    case kb::render::RenderMaterialGraphNodeKind::SrgbToLinear:
    case kb::render::RenderMaterialGraphNodeKind::LinearToSrgb:
    case kb::render::RenderMaterialGraphNodeKind::Logarithm10:
    case kb::render::RenderMaterialGraphNodeKind::HsvToRgb:
    case kb::render::RenderMaterialGraphNodeKind::RgbToHsv:
    case kb::render::RenderMaterialGraphNodeKind::DeriveNormalZ:
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Step:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::If:
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        break;
    }
    return {};
}

void EnsureEditableRenderMaterialGraph(RenderMaterialGraphDocument& graph) {
    if (graph.nodes.empty()) {
        graph = MakeDefaultRenderMaterialGraphDocument();
    }
}

std::uint32_t NextRenderMaterialGraphNodeId(const RenderMaterialGraphDocument& graph) noexcept {
    std::uint32_t next = 1U;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        next = std::max(next, node.id + 1U);
    }
    return next;
}

RenderMaterialGraphNode* FindMutableRenderMaterialGraphNode(RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    for (RenderMaterialGraphNode& node : graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

bool IsRenderMaterialGraphConstantNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::ConstantScalar ||
        kind == RenderMaterialGraphNodeKind::ConstantBool ||
        kind == RenderMaterialGraphNodeKind::ConstantVector2 ||
        kind == RenderMaterialGraphNodeKind::ConstantVector ||
        kind == RenderMaterialGraphNodeKind::ConstantColor;
}

std::string_view RenderMaterialGraphConstantDisplayName(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return "Scalar";
    case RenderMaterialGraphNodeKind::ConstantBool:
        return "Bool";
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return "XY";
    case RenderMaterialGraphNodeKind::ConstantVector:
        return "RGB Node";
    case RenderMaterialGraphNodeKind::ConstantColor:
        return "RGBA Node";
    default:
        return "Constant";
    }
}

std::size_t RenderMaterialGraphConstantComponentCount(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantBool:
        return 1U;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return 2U;
    case RenderMaterialGraphNodeKind::ConstantVector:
        return 3U;
    case RenderMaterialGraphNodeKind::ConstantColor:
        return 4U;
    default:
        return 0U;
    }
}

std::optional<bool> ParseRenderMaterialGraphConstantBool(std::string_view text) noexcept {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        if (!std::isspace(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "false" || normalized == "0") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::array<float, 4U>> ParseRenderMaterialGraphConstantValue(
    RenderMaterialGraphNodeKind kind,
    std::string_view text) {
    std::array<float, 4U> value{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::vector<float> numericValues;
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        if (ParseFiniteMaterialFloatSequence(text, numericValues, 1U, 1U)) {
            value[0] = numericValues[0];
            return value;
        }
        return std::nullopt;
    case RenderMaterialGraphNodeKind::ConstantBool:
        if (const std::optional<bool> parsed = ParseRenderMaterialGraphConstantBool(text)) {
            value[0] = *parsed ? 1.0F : 0.0F;
            return value;
        }
        return std::nullopt;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        if (ParseFiniteMaterialFloatSequence(text, numericValues, 2U, 2U)) {
            std::copy(numericValues.begin(), numericValues.end(), value.begin());
            return value;
        }
        return std::nullopt;
    case RenderMaterialGraphNodeKind::ConstantVector:
        if (ParseFiniteMaterialFloatSequence(text, numericValues, 3U, 3U)) {
            std::copy(numericValues.begin(), numericValues.end(), value.begin());
            return value;
        }
        return std::nullopt;
    case RenderMaterialGraphNodeKind::ConstantColor:
        if (ParseFiniteMaterialFloatSequence(text, numericValues, 3U, 4U)) {
            std::copy(numericValues.begin(), numericValues.end(), value.begin());
            value[3] = numericValues.size() == 4U ? numericValues[3] : 1.0F;
            return value;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::string RenderMaterialGraphFloatText(float value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

std::string RenderMaterialGraphConstantValueHint(RenderMaterialGraphNodeKind kind, const std::array<float, 4U>& value) {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return RenderMaterialGraphFloatText(value[0]);
    case RenderMaterialGraphNodeKind::ConstantBool:
        return value[0] != 0.0F ? "true" : "false";
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return RenderMaterialGraphFloatText(value[0]) + " " + RenderMaterialGraphFloatText(value[1]);
    case RenderMaterialGraphNodeKind::ConstantVector:
        return RenderMaterialGraphFloatText(value[0]) + " " + RenderMaterialGraphFloatText(value[1]) + " " +
            RenderMaterialGraphFloatText(value[2]);
    case RenderMaterialGraphNodeKind::ConstantColor:
        return RenderMaterialGraphFloatText(value[0]) + " " + RenderMaterialGraphFloatText(value[1]) + " " +
            RenderMaterialGraphFloatText(value[2]) + " " + RenderMaterialGraphFloatText(value[3]);
    default:
        return {};
    }
}

std::optional<RenderMaterialParameterRange> RenderMaterialGraphConstantRange(const RenderMaterialGraphNode& node) noexcept {
    if (!node.parameter.hasRange) {
        return std::nullopt;
    }
    return RenderMaterialParameterRange{
        .min = node.parameter.rangeMin,
        .max = node.parameter.rangeMax,
    };
}

void ClampRenderMaterialGraphConstantValues(const RenderMaterialGraphNode& node, std::array<float, 4U>& values) noexcept {
    const std::optional<RenderMaterialParameterRange> range = RenderMaterialGraphConstantRange(node);
    if (!range.has_value()) {
        return;
    }
    const std::size_t componentCount = RenderMaterialGraphConstantComponentCount(node.kind);
    for (std::size_t index = 0U; index < componentCount; ++index) {
        values[index] = std::clamp(values[index], range->min, range->max);
    }
}

std::optional<RenderMaterialGraphNodeKind> RenderMaterialGraphPromotedParameterKind(RenderMaterialGraphNodeKind sourceKind) noexcept {
    switch (sourceKind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return RenderMaterialGraphNodeKind::ParameterScalar;
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
        return RenderMaterialGraphNodeKind::ParameterVector;
    case RenderMaterialGraphNodeKind::ConstantColor:
        return RenderMaterialGraphNodeKind::ParameterColor;
    default:
        return std::nullopt;
    }
}

std::string RenderMaterialGraphPromotedSourceOutputPin(RenderMaterialGraphNodeKind sourceKind) {
    switch (sourceKind) {
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return "xy";
    case RenderMaterialGraphNodeKind::ConstantVector:
        return "xyz";
    case RenderMaterialGraphNodeKind::ConstantColor:
        return "rgba";
    case RenderMaterialGraphNodeKind::ConstantScalar:
    default:
        return "value";
    }
}

std::string RenderMaterialGraphPromotedTargetOutputPin(RenderMaterialGraphNodeKind targetKind) {
    switch (targetKind) {
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "xyz";
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "rgba";
    case RenderMaterialGraphNodeKind::ParameterScalar:
    default:
        return "value";
    }
}

std::string RenderMaterialGraphPromotedParameterDefaultValueHint(
    const RenderMaterialGraphNode& sourceNode,
    RenderMaterialGraphNodeKind targetKind) {
    std::array<float, 4U> values{};
    if (const std::optional<std::array<float, 4U>> parsed =
            ParseRenderMaterialGraphConstantValue(sourceNode.kind, sourceNode.parameter.defaultValueHint)) {
        values = *parsed;
    }
    switch (targetKind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return RenderMaterialGraphConstantValueHint(RenderMaterialGraphNodeKind::ConstantScalar, values);
    case RenderMaterialGraphNodeKind::ParameterVector:
        return RenderMaterialGraphConstantValueHint(RenderMaterialGraphNodeKind::ConstantVector, values);
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialGraphConstantValueHint(RenderMaterialGraphNodeKind::ConstantColor, values);
    default:
        return sourceNode.parameter.defaultValueHint;
    }
}

std::optional<std::uint32_t> AddRenderMaterialGraphNode(
    RenderMaterialGraphDocument& graph,
    RenderMaterialGraphNodeKind kind,
    std::int32_t positionX,
    std::int32_t positionY) {
    if (kind == RenderMaterialGraphNodeKind::MaterialOutput) {
        return std::nullopt;
    }
    EnsureEditableRenderMaterialGraph(graph);
    const std::uint32_t nodeId = NextRenderMaterialGraphNodeId(graph);
    RenderMaterialGraphNode node{
        .id = nodeId,
        .kind = kind,
        .positionX = positionX,
        .positionY = positionY,
        .parameter = DefaultRenderMaterialGraphParameterMetadata(kind, nodeId),
    };
    if (kind == RenderMaterialGraphNodeKind::CustomCode) {
        node.customCode = DefaultRenderMaterialGraphCustomCode();
    } else if (kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        node.customCode = DefaultRenderMaterialGraphFunctionCall();
    }
    graph.nodes.push_back(std::move(node));
    return nodeId;
}

bool ConnectRenderMaterialGraphPins(
    RenderMaterialGraphDocument& graph,
    std::uint32_t fromNodeId,
    std::string_view fromPin,
    std::uint32_t toNodeId,
    std::string_view toPin) {
    if (fromNodeId == 0U || toNodeId == 0U || fromNodeId == toNodeId) {
        return false;
    }
    RenderMaterialGraphDocument edited = graph;
    EnsureEditableRenderMaterialGraph(edited);
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(edited, fromNodeId);
    const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(edited, toNodeId);
    if (fromNode == nullptr || toNode == nullptr ||
        !IsRenderMaterialGraphOutputPin(*fromNode, fromPin) ||
        !IsRenderMaterialGraphInputPin(*toNode, toPin) ||
        !AreRenderMaterialGraphPinsCompatible(*fromNode, fromPin, *toNode, toPin)) {
        return false;
    }
    RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = RenderMaterialGraphStablePinId(*fromNode, fromPin, true),
        .fromPin = std::string{ fromPin },
        .toNodeId = toNodeId,
        .toPinId = RenderMaterialGraphStablePinId(*toNode, toPin, false),
        .toPin = std::string{ toPin },
    };
    if (link.fromPinId == 0U || link.toPinId == 0U) {
        return false;
    }
    link.id = MakeRenderMaterialGraphLinkId(link);
    const auto oldInputEnd = std::remove_if(edited.links.begin(), edited.links.end(), [toNodeId, toPin](const RenderMaterialGraphLink& existing) {
        return existing.toNodeId == toNodeId && existing.toPin == toPin;
    });
    edited.links.erase(oldInputEnd, edited.links.end());
    if (FindRenderMaterialGraphLink(edited, link.id) == nullptr) {
        edited.links.push_back(std::move(link));
    }
    graph = std::move(edited);
    return true;
}

bool SetRenderMaterialGraphConstantValue(
    RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view valueText,
    std::string* normalizedHint) {
    RenderMaterialGraphNode* node = FindMutableRenderMaterialGraphNode(graph, nodeId);
    if (node == nullptr || !IsRenderMaterialGraphConstantNode(node->kind)) {
        return false;
    }
    std::optional<std::array<float, 4U>> parsed = ParseRenderMaterialGraphConstantValue(node->kind, valueText);
    if (!parsed.has_value()) {
        return false;
    }
    ClampRenderMaterialGraphConstantValues(*node, *parsed);
    const std::string nextValueHint = RenderMaterialGraphConstantValueHint(node->kind, *parsed);
    node->parameter.defaultValueHint = nextValueHint;
    node->parameter.overrideSupported = false;
    if (node->parameter.displayName.empty()) {
        node->parameter.displayName = RenderMaterialGraphConstantDisplayName(node->kind);
    }
    if (normalizedHint != nullptr) {
        *normalizedHint = nextValueHint;
    }
    return true;
}

bool PromoteRenderMaterialGraphNodeToParameter(RenderMaterialGraphDocument& graph, std::uint32_t nodeId) {
    RenderMaterialGraphDocument edited = graph;
    RenderMaterialGraphNode* node = FindMutableRenderMaterialGraphNode(edited, nodeId);
    if (node == nullptr) {
        return false;
    }
    const RenderMaterialGraphNodeKind sourceKind = node->kind;
    const std::optional<RenderMaterialGraphNodeKind> targetKind = RenderMaterialGraphPromotedParameterKind(sourceKind);
    if (!targetKind.has_value()) {
        return false;
    }
    const std::string sourcePin = RenderMaterialGraphPromotedSourceOutputPin(sourceKind);
    const std::string targetPin = RenderMaterialGraphPromotedTargetOutputPin(*targetKind);
    RenderMaterialGraphParameterMetadata metadata = DefaultRenderMaterialGraphParameterMetadata(*targetKind, node->id);
    metadata.defaultValueHint = RenderMaterialGraphPromotedParameterDefaultValueHint(*node, *targetKind);
    if (node->parameter.hasRange) {
        metadata.hasRange = node->parameter.hasRange;
        metadata.rangeMin = node->parameter.rangeMin;
        metadata.rangeMax = node->parameter.rangeMax;
    }
    metadata.overrideSupported = true;
    node->kind = *targetKind;
    node->parameter = std::move(metadata);
    const std::uint32_t targetPinId = RenderMaterialGraphStablePinId(*node, targetPin, true);
    if (targetPinId == 0U) {
        return false;
    }
    for (RenderMaterialGraphLink& link : edited.links) {
        if (link.fromNodeId == node->id && link.fromPin == sourcePin) {
            link.fromPin = targetPin;
            link.fromPinId = targetPinId;
            link.id = MakeRenderMaterialGraphLinkId(link);
        }
    }
    graph = std::move(edited);
    return true;
}

std::string NormalizeRenderMaterialGraphNodeDisplayName(std::string_view displayName, std::string_view fallbackName) {
    const std::size_t first = displayName.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return std::string{ fallbackName };
    }
    const std::size_t last = displayName.find_last_not_of(" \t\r\n");
    std::string normalized{ displayName.substr(first, last - first + 1U) };
    constexpr std::size_t kMaxGraphNodeDisplayNameLength = 64U;
    if (normalized.size() > kMaxGraphNodeDisplayNameLength) {
        normalized.resize(kMaxGraphNodeDisplayNameLength);
    }
    return normalized;
}

bool RenameRenderMaterialGraphNode(
    RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view displayName,
    std::string_view fallbackName) {
    RenderMaterialGraphNode* node = FindMutableRenderMaterialGraphNode(graph, nodeId);
    if (node == nullptr) {
        return false;
    }
    const std::string normalized = NormalizeRenderMaterialGraphNodeDisplayName(displayName, fallbackName);
    if (node->parameter.displayName == normalized) {
        return false;
    }
    node->parameter.displayName = normalized;
    return true;
}

bool IsRenderMaterialGraphTextureAssetNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::TextureSample ||
        kind == RenderMaterialGraphNodeKind::TextureSampleCube ||
        kind == RenderMaterialGraphNodeKind::TextureSampleVolume ||
        kind == RenderMaterialGraphNodeKind::TextureSample2DArray ||
        kind == RenderMaterialGraphNodeKind::ParameterTexture ||
        kind == RenderMaterialGraphNodeKind::TextureObject ||
        kind == RenderMaterialGraphNodeKind::TextureObjectCube ||
        kind == RenderMaterialGraphNodeKind::TextureObjectVolume ||
        kind == RenderMaterialGraphNodeKind::TextureObject2DArray;
}

bool SetRenderMaterialGraphTextureNodeAsset(
    RenderMaterialAssetData& material,
    std::uint32_t nodeId,
    std::uint64_t textureAssetId) {
    RenderMaterialGraphNode* node = FindMutableRenderMaterialGraphNode(material.graph, nodeId);
    if (node == nullptr || !IsRenderMaterialGraphTextureAssetNode(node->kind)) {
        return false;
    }
    const bool textureObject = node->kind == RenderMaterialGraphNodeKind::TextureObject ||
        node->kind == RenderMaterialGraphNodeKind::TextureObjectCube ||
        node->kind == RenderMaterialGraphNodeKind::TextureObjectVolume ||
        node->kind == RenderMaterialGraphNodeKind::TextureObject2DArray;
    if (node->parameter.stableId.empty()) {
        if (node->kind == RenderMaterialGraphNodeKind::ParameterTexture) {
            node->parameter.stableId = "texture" + std::to_string(node->id);
        } else if (textureObject) {
            node->parameter.stableId = "textureObject" + std::to_string(node->id);
        } else {
            node->parameter.stableId = "textureSample" + std::to_string(node->id);
        }
    }
    if (node->parameter.displayName.empty()) {
        if (node->kind == RenderMaterialGraphNodeKind::ParameterTexture) {
            node->parameter.displayName = "Texture " + std::to_string(node->id);
        } else if (textureObject) {
            node->parameter.displayName = "Texture Object " + std::to_string(node->id);
        } else {
            node->parameter.displayName = "Texture Sample " + std::to_string(node->id);
        }
    }
    if (node->parameter.textureRole.empty()) {
        node->parameter.textureRole = "baseColor";
    }
    if (node->parameter.expectedTextureColorSpace == RenderMaterialTextureColorSpace::Unknown) {
        node->parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    }
    node->parameter.overrideSupported = true;
    const std::string stableId = node->parameter.stableId;
    auto existing = std::ranges::find_if(material.graphParameterValues, [&stableId](const RenderMaterialGraphParameterValue& value) {
        return value.stableId == stableId && value.type == RenderMaterialParameterType::Texture;
    });
    if (existing != material.graphParameterValues.end()) {
        existing->assetId = textureAssetId;
    } else {
        material.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
            .stableId = stableId,
            .type = RenderMaterialParameterType::Texture,
            .assetId = textureAssetId,
        });
    }
    return true;
}

std::optional<RenderMaterialGraphParameterValue> ParseRenderMaterialGraphParameterValue(
    std::string_view stableId,
    RenderMaterialParameterType type,
    std::string_view text) {
    RenderMaterialGraphParameterValue value{};
    value.stableId = std::string{ stableId };
    value.type = type;

    std::vector<float> numbers;
    switch (type) {
    case RenderMaterialParameterType::Scalar:
        if (ParseFiniteMaterialFloatSequence(text, numbers, 1U, 1U)) {
            value.numbers[0] = numbers[0];
            return value;
        }
        return std::nullopt;
    case RenderMaterialParameterType::Vec3:
        if (ParseFiniteMaterialFloatSequence(text, numbers, 3U, 3U)) {
            std::copy_n(numbers.begin(), 3U, value.numbers.begin());
            return value;
        }
        return std::nullopt;
    case RenderMaterialParameterType::Vec4:
    case RenderMaterialParameterType::Color:
        if (ParseFiniteMaterialFloatSequence(text, numbers, 3U, 4U)) {
            std::copy(numbers.begin(), numbers.end(), value.numbers.begin());
            value.numbers[3] = numbers.size() == 4U ? numbers[3] : 1.0F;
            return value;
        }
        return std::nullopt;
    case RenderMaterialParameterType::Bool: {
        std::istringstream input{ std::string{ text } };
        std::string boolText;
        std::string trailing;
        if (!(input >> boolText) || (input >> trailing)) {
            return std::nullopt;
        }
        std::ranges::transform(boolText, boolText.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (boolText == "true" || boolText == "1" || boolText == "yes" || boolText == "on") {
            value.boolValue = true;
            return value;
        }
        if (boolText == "false" || boolText == "0" || boolText == "no" || boolText == "off") {
            value.boolValue = false;
            return value;
        }
        return std::nullopt;
    }
    case RenderMaterialParameterType::Enum:
        value.text = std::string{ text };
        return value.text.empty() ? std::nullopt : std::optional<RenderMaterialGraphParameterValue>{ value };
    case RenderMaterialParameterType::Texture:
        return std::nullopt;
    }
    return std::nullopt;
}

void UpsertRenderMaterialGraphParameterValue(RenderMaterialAssetData& material, RenderMaterialGraphParameterValue value) {
    for (RenderMaterialGraphParameterValue& existing : material.graphParameterValues) {
        if (existing.stableId == value.stableId) {
            existing = std::move(value);
            return;
        }
    }
    material.graphParameterValues.push_back(std::move(value));
}

void RemoveRenderMaterialGraphParameterValue(RenderMaterialAssetData& material, std::string_view stableId) {
    const auto oldEnd = std::remove_if(material.graphParameterValues.begin(), material.graphParameterValues.end(),
        [stableId](const RenderMaterialGraphParameterValue& value) {
            return value.stableId == stableId;
        });
    material.graphParameterValues.erase(oldEnd, material.graphParameterValues.end());
}

void EnsureRenderMaterialInstanceOverrideDocument(
    RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial) {
    if (!instance.hasOverrides) {
        instance.hasOverrides = true;
        instance.overrides = RenderMaterialAssetData{};
    }
    instance.overrides.materialType = parentMaterial.materialType;
    instance.overrides.materialTypeVersion = parentMaterial.materialTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    if (instance.overrides.materialTypeAssetId == 0U) {
        instance.overrides.materialTypeAssetId = parentMaterial.materialTypeAssetId;
    }
    if (instance.overrides.materialTypeAssetPath.empty()) {
        instance.overrides.materialTypeAssetPath = parentMaterial.materialTypeAssetPath;
    }
    if (instance.overrides.graphSourceAssetId == 0U) {
        instance.overrides.graphSourceAssetId = parentMaterial.graphSourceAssetId;
    }
    if (instance.overrides.graphSourceAssetPath.empty()) {
        instance.overrides.graphSourceAssetPath = parentMaterial.graphSourceAssetPath;
    }
}

RenderMaterialInstanceValidationResult SetRenderMaterialInstanceParameterOverride(
    RenderMaterialInstanceAssetData& instance,
    const RenderMaterialAssetData& parentMaterial,
    RenderMaterialGraphParameterValue value) {
    RenderMaterialInstanceAssetData edited = instance;
    EnsureRenderMaterialInstanceOverrideDocument(edited, parentMaterial);
    RemoveRenderMaterialGraphParameterValue(edited.overrides, value.stableId);
    UpsertRenderMaterialGraphParameterValue(edited.overrides, std::move(value));
    RenderMaterialInstanceValidationResult validation =
        RenderMaterialInstanceAssetLoader::ValidateAgainstParent(edited, parentMaterial);
    if (validation.Succeeded()) {
        instance = std::move(edited);
    }
    return validation;
}

} // namespace kb::render
