#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

std::atomic<std::uint64_t> g_renderMaterialGraphCompileInvocationCount{ 0U };

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

void HashString64(std::uint64_t& hash, std::string_view value) noexcept {
    for (const char ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
}

[[nodiscard]] std::string EncodeToken(std::string_view value) {
    std::string encoded;
    for (const char ch : value) {
        switch (ch) {
        case '%': encoded += "%25"; break;
        case ' ': encoded += "%20"; break;
        case '\t': encoded += "%09"; break;
        case '#': encoded += "%23"; break;
        default: encoded += ch; break;
        }
    }
    return encoded;
}

[[nodiscard]] std::vector<float> ParseDefaultNumbers(std::string_view text) {
    std::vector<float> values;
    if (text.empty() || text == "_") {
        return values;
    }
    std::istringstream input{ std::string{ text } };
    float value = 0.0F;
    while (input >> value) {
        values.push_back(value);
    }
    return values;
}

[[nodiscard]] std::string FloatLiteral(float value) {
    std::ostringstream output;
    output << value;
    std::string text = output.str();
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos && text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

[[nodiscard]] bool IsRenderMaterialGraphConstantNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::ConstantScalar ||
        kind == RenderMaterialGraphNodeKind::ConstantVector2 ||
        kind == RenderMaterialGraphNodeKind::ConstantVector ||
        kind == RenderMaterialGraphNodeKind::ConstantColor;
}

[[nodiscard]] bool ShouldPersistGraphNodeMetadata(const RenderMaterialGraphNode& node) noexcept {
    return IsRenderMaterialGraphParameterNode(node.kind) ||
        IsRenderMaterialGraphConstantNode(node.kind) ||
        (node.kind == RenderMaterialGraphNodeKind::TextureSample && !node.parameter.stableId.empty()) ||
        (node.kind == RenderMaterialGraphNodeKind::Uv && !node.parameter.defaultValueHint.empty());
}

[[nodiscard]] std::string_view ParameterGroupName(RenderMaterialParameterGroup group) noexcept {
    switch (group) {
    case RenderMaterialParameterGroup::Core: return "Core";
    case RenderMaterialParameterGroup::Surface: return "Surface";
    case RenderMaterialParameterGroup::Advanced: return "Advanced";
    }
    return "Core";
}

[[nodiscard]] std::string_view TextureColorSpaceName(RenderMaterialTextureColorSpace colorSpace) noexcept {
    switch (colorSpace) {
    case RenderMaterialTextureColorSpace::Srgb: return "Srgb";
    case RenderMaterialTextureColorSpace::Linear: return "Linear";
    case RenderMaterialTextureColorSpace::Unknown: return "Unknown";
    }
    return "Unknown";
}

void AddGraphDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    RenderMaterialGraphDiagnosticKind kind,
    std::uint32_t nodeId,
    std::uint32_t linkId,
    std::string pin,
    std::string message) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = severity,
        .kind = kind,
        .nodeId = nodeId,
        .linkId = linkId,
        .pin = std::move(pin),
        .message = std::move(message),
    });
}

[[nodiscard]] std::string StableParameterId(const RenderMaterialGraphNode& node);

void AppendIrPin(RenderMaterialGraphIrNode& irNode, RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) {
    RenderMaterialGraphIrPin typedPin{
        .name = std::string{ pin },
        .stablePinId = RenderMaterialGraphStablePinId(kind, pin, outputPin),
        .type = RenderMaterialGraphPinDataType(kind, pin, outputPin),
        .outputPin = outputPin,
    };
    if (outputPin) {
        irNode.outputs.push_back(std::move(typedPin));
    } else {
        irNode.inputs.push_back(std::move(typedPin));
    }
}

void AppendIrPins(RenderMaterialGraphIrNode& irNode) {
    switch (irNode.kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        AppendIrPin(irNode, irNode.kind, "baseColor", false);
        AppendIrPin(irNode, irNode.kind, "metallic", false);
        AppendIrPin(irNode, irNode.kind, "roughness", false);
        AppendIrPin(irNode, irNode.kind, "normal", false);
        AppendIrPin(irNode, irNode.kind, "emissive", false);
        AppendIrPin(irNode, irNode.kind, "occlusion", false);
        AppendIrPin(irNode, irNode.kind, "alpha", false);
        AppendIrPin(irNode, irNode.kind, "alphaClipThreshold", false);
        AppendIrPin(irNode, irNode.kind, "worldPositionOffset", false);
        AppendIrPin(irNode, irNode.kind, "specular", false);
        AppendIrPin(irNode, irNode.kind, "anisotropy", false);
        AppendIrPin(irNode, irNode.kind, "tangent", false);
        AppendIrPin(irNode, irNode.kind, "subsurfaceColor", false);
        AppendIrPin(irNode, irNode.kind, "clearCoat", false);
        AppendIrPin(irNode, irNode.kind, "clearCoatRoughness", false);
        AppendIrPin(irNode, irNode.kind, "refraction", false);
        AppendIrPin(irNode, irNode.kind, "surfaceThickness", false);
        AppendIrPin(irNode, irNode.kind, "attributes", false);
        break;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "baseColor", false);
        AppendIrPin(irNode, irNode.kind, "metallic", false);
        AppendIrPin(irNode, irNode.kind, "roughness", false);
        AppendIrPin(irNode, irNode.kind, "normal", false);
        AppendIrPin(irNode, irNode.kind, "emissive", false);
        AppendIrPin(irNode, irNode.kind, "occlusion", false);
        AppendIrPin(irNode, irNode.kind, "alpha", false);
        AppendIrPin(irNode, irNode.kind, "attributes", true);
        break;
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "attributes", false);
        AppendIrPin(irNode, irNode.kind, "baseColor", true);
        AppendIrPin(irNode, irNode.kind, "metallic", true);
        AppendIrPin(irNode, irNode.kind, "roughness", true);
        AppendIrPin(irNode, irNode.kind, "normal", true);
        AppendIrPin(irNode, irNode.kind, "emissive", true);
        AppendIrPin(irNode, irNode.kind, "occlusion", true);
        AppendIrPin(irNode, irNode.kind, "alpha", true);
        break;
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "factor", false);
        AppendIrPin(irNode, irNode.kind, "attributes", true);
        break;
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "attributes", false);
        AppendIrPin(irNode, irNode.kind, "baseColor", true);
        AppendIrPin(irNode, irNode.kind, "metallic", true);
        AppendIrPin(irNode, irNode.kind, "roughness", true);
        AppendIrPin(irNode, irNode.kind, "normal", true);
        AppendIrPin(irNode, irNode.kind, "emissive", true);
        AppendIrPin(irNode, irNode.kind, "occlusion", true);
        AppendIrPin(irNode, irNode.kind, "alpha", true);
        break;
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "attributes", false);
        AppendIrPin(irNode, irNode.kind, "baseColor", false);
        AppendIrPin(irNode, irNode.kind, "metallic", false);
        AppendIrPin(irNode, irNode.kind, "roughness", false);
        AppendIrPin(irNode, irNode.kind, "normal", false);
        AppendIrPin(irNode, irNode.kind, "emissive", false);
        AppendIrPin(irNode, irNode.kind, "occlusion", false);
        AppendIrPin(irNode, irNode.kind, "alpha", false);
        AppendIrPin(irNode, irNode.kind, "attributesOut", true);
        break;
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::StaticSwitch:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "true", false);
        AppendIrPin(irNode, irNode.kind, "false", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        AppendIrPin(irNode, irNode.kind, "input", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::TextureCoordinate:
    case RenderMaterialGraphNodeKind::ViewportUV:
        AppendIrPin(irNode, irNode.kind, "uv", true);
        break;
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
        AppendIrPin(irNode, irNode.kind, "coordinate", false);
        AppendIrPin(irNode, irNode.kind, "time", false);
        AppendIrPin(irNode, irNode.kind, "uv", true);
        break;
    case RenderMaterialGraphNodeKind::BumpOffset:
        AppendIrPin(irNode, irNode.kind, "coordinate", false);
        AppendIrPin(irNode, irNode.kind, "height", false);
        AppendIrPin(irNode, irNode.kind, "uv", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        AppendIrPin(irNode, irNode.kind, "input", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        AppendIrPin(irNode, irNode.kind, "axis", false);
        AppendIrPin(irNode, irNode.kind, "angle", false);
        AppendIrPin(irNode, irNode.kind, "position", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::CameraPosition:
    case RenderMaterialGraphNodeKind::CameraVector:
    case RenderMaterialGraphNodeKind::ReflectionVector:
    case RenderMaterialGraphNodeKind::LightVector:
    case RenderMaterialGraphNodeKind::PixelNormalWS:
    case RenderMaterialGraphNodeKind::VertexNormalWS:
    case RenderMaterialGraphNodeKind::VertexTangentWS:
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::TextureSample:
        AppendIrPin(irNode, irNode.kind, "texture", false);
        AppendIrPin(irNode, irNode.kind, "uv", false);
        AppendIrPin(irNode, irNode.kind, "color", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        AppendIrPin(irNode, irNode.kind, "a", true);
        break;
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Power:
        AppendIrPin(irNode, irNode.kind, "base", false);
        AppendIrPin(irNode, irNode.kind, "exponent", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::BreakVector:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "x", true);
        AppendIrPin(irNode, irNode.kind, "y", true);
        AppendIrPin(irNode, irNode.kind, "z", true);
        AppendIrPin(irNode, irNode.kind, "w", true);
        break;
    case RenderMaterialGraphNodeKind::MakeVector:
        AppendIrPin(irNode, irNode.kind, "x", false);
        AppendIrPin(irNode, irNode.kind, "y", false);
        AppendIrPin(irNode, irNode.kind, "z", false);
        AppendIrPin(irNode, irNode.kind, "w", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Step:
        AppendIrPin(irNode, irNode.kind, "edge", false);
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::SmoothStep:
        AppendIrPin(irNode, irNode.kind, "min", false);
        AppendIrPin(irNode, irNode.kind, "max", false);
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::If:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "less", false);
        AppendIrPin(irNode, irNode.kind, "equal", false);
        AppendIrPin(irNode, irNode.kind, "greater", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Desaturate:
        AppendIrPin(irNode, irNode.kind, "color", false);
        AppendIrPin(irNode, irNode.kind, "fraction", false);
        AppendIrPin(irNode, irNode.kind, "color", true);
        break;
    case RenderMaterialGraphNodeKind::Fresnel:
        AppendIrPin(irNode, irNode.kind, "normal", false);
        AppendIrPin(irNode, irNode.kind, "view", false);
        AppendIrPin(irNode, irNode.kind, "exponent", false);
        AppendIrPin(irNode, irNode.kind, "base", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::ArcTangent2:
        AppendIrPin(irNode, irNode.kind, "y", false);
        AppendIrPin(irNode, irNode.kind, "x", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Clamp:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "min", false);
        AppendIrPin(irNode, irNode.kind, "max", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Lerp:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "t", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        AppendIrPin(irNode, irNode.kind, "color", false);
        AppendIrPin(irNode, irNode.kind, "normal", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        AppendIrPin(irNode, irNode.kind, "xy", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        AppendIrPin(irNode, irNode.kind, "rgba", true);
        break;
    case RenderMaterialGraphNodeKind::ParameterTexture:
        AppendIrPin(irNode, irNode.kind, "texture", true);
        break;
    case RenderMaterialGraphNodeKind::Uv:
        AppendIrPin(irNode, irNode.kind, "uv", true);
        break;
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::VertexColor:
        AppendIrPin(irNode, irNode.kind, "rgba", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        AppendIrPin(irNode, irNode.kind, "a", true);
        break;
    case RenderMaterialGraphNodeKind::ScreenPosition:
        AppendIrPin(irNode, irNode.kind, "xy", true);
        break;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        break;
    }
}

[[nodiscard]] std::uint32_t DiagnosticPinId(const RenderMaterialGraphDocument& graph, const RenderMaterialGraphDiagnostic& diagnostic) noexcept {
    if (diagnostic.pinId != 0U || diagnostic.nodeId == 0U || diagnostic.pin.empty()) {
        return diagnostic.pinId;
    }
    const RenderMaterialGraphNode* node = FindRenderMaterialGraphNode(graph, diagnostic.nodeId);
    if (node == nullptr) {
        return 0U;
    }
    std::uint32_t pinId = RenderMaterialGraphStablePinId(node->kind, diagnostic.pin, false);
    if (pinId == 0U) {
        pinId = RenderMaterialGraphStablePinId(node->kind, diagnostic.pin, true);
    }
    return pinId;
}

void AttachDiagnosticContext(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphBuildContext& context,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    for (RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        diagnostic.assetId = context.assetId;
        diagnostic.sourcePath = context.sourcePath;
        diagnostic.pinId = DiagnosticPinId(graph, diagnostic);
    }
}

[[nodiscard]] std::string SanitizeShaderIdentifier(std::string_view text, std::string_view fallback) {
    std::string sanitized;
    sanitized.reserve(text.empty() ? fallback.size() : text.size());
    const std::string_view source = text.empty() ? fallback : text;
    for (const char ch : source) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty() || (sanitized[0] >= '0' && sanitized[0] <= '9')) {
        sanitized.insert(sanitized.begin(), '_');
    }
    return sanitized;
}

[[nodiscard]] const RenderMaterialGraphLink* FindInputLink(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == nodeId && link.toPin == pin) {
            return &link;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ContainsNode(std::span<const std::uint32_t> stack, std::uint32_t nodeId) noexcept {
    return std::find(stack.begin(), stack.end(), nodeId) != stack.end();
}

[[nodiscard]] std::string DefaultExpressionForType(RenderMaterialGraphPinType type) {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return "0.0";
    case RenderMaterialGraphPinType::Float2:
        return "vec2(0.0, 0.0)";
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return "vec3(0.0, 0.0, 1.0)";
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
        return "vec4(1.0, 1.0, 1.0, 1.0)";
    case RenderMaterialGraphPinType::Bool:
        return "false";
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::Unknown:
        break;
    }
    return "0.0";
}

[[nodiscard]] std::string CoerceExpression(std::string expression, RenderMaterialGraphPinType from, RenderMaterialGraphPinType to) {
    if (from == to || to == RenderMaterialGraphPinType::Unknown) {
        return expression;
    }
    if (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float3) {
        return "(" + expression + ").rgb";
    }
    if (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float4) {
        return expression;
    }
    if (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Color) {
        return "vec4(" + expression + ", 1.0)";
    }
    if (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Float4) {
        return "vec4(" + expression + ", 1.0)";
    }
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float3) {
        return "(" + expression + ").xyz";
    }
    if (from == RenderMaterialGraphPinType::Normal && to == RenderMaterialGraphPinType::Float3) {
        return expression;
    }
    if (from == RenderMaterialGraphPinType::Normal && to == RenderMaterialGraphPinType::Float4) {
        return "vec4(" + expression + ", 1.0)";
    }
    if (from == RenderMaterialGraphPinType::Float && to == RenderMaterialGraphPinType::Float4) {
        // bgfx maps vec4 -> float4 textually, so vec4(scalar) is invalid HLSL; splat explicitly.
        return "vec4_splat(" + expression + ")";
    }
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
    }
    return expression;
}

struct GraphCodegen {
    const RenderMaterialGraphDocument& graph;
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics;
    std::vector<std::uint32_t> stack;
    std::unordered_map<std::uint32_t, std::uint32_t> fanOut;
    std::unordered_map<std::uint32_t, std::string> emittedTemp;
    std::string statements;
};

[[nodiscard]] std::string GraphCodegenGlslType(RenderMaterialGraphPinType type) {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return "float";
    case RenderMaterialGraphPinType::Float2:
        return "vec2";
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return "vec3";
    case RenderMaterialGraphPinType::MaterialAttributes:
        return "MaterialSurface";
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::Bool:
        return "vec4";
    }
    return "vec4";
}

[[nodiscard]] RenderMaterialGraphPinType GraphNodeCanonicalType(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::TextureSample:
        return RenderMaterialGraphPinType::Color;
    case RenderMaterialGraphNodeKind::BreakVector:
        return RenderMaterialGraphPinType::Float4;
    case RenderMaterialGraphNodeKind::Desaturate:
        return RenderMaterialGraphPinDataType(kind, "color", true);
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return RenderMaterialGraphPinDataType(kind, "normal", true);
    default:
        return RenderMaterialGraphPinDataType(kind, "value", true);
    }
}

[[nodiscard]] bool IsGraphCseCandidate(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        return false;
    default:
        return true;
    }
}

[[nodiscard]] std::string CompileNodeOutputExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node, std::string_view outputPin);
[[nodiscard]] std::string CompileNodeBaseExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node);
[[nodiscard]] std::string SelectGraphPinFromBase(GraphCodegen& cg, const RenderMaterialGraphNode& node, const std::string& baseRef, std::string_view outputPin);

[[nodiscard]] std::string CompileInputExpression(
    GraphCodegen& cg,
    const RenderMaterialGraphNode& node,
    std::string_view inputPin,
    RenderMaterialGraphPinType expectedType,
    std::string fallback) {
    const RenderMaterialGraphLink* link = FindInputLink(cg.graph, node.id, inputPin);
    if (link == nullptr) {
        return fallback;
    }
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(cg.graph, link->fromNodeId);
    if (fromNode == nullptr) {
        return fallback;
    }
    const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, link->fromPin, true);
    return CoerceExpression(
        CompileNodeOutputExpression(cg, *fromNode, link->fromPin),
        fromType,
        expectedType);
}

void AddShaderGenerationDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    const RenderMaterialGraphNode& node,
    std::string_view pin,
    std::string message) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .nodeId = node.id,
        .pinId = RenderMaterialGraphStablePinId(node.kind, pin, true),
        .pin = std::string{ pin },
        .message = std::move(message),
    });
}

[[nodiscard]] std::string ParameterUniformName(const RenderMaterialGraphNode& node, std::string_view suffix) {
    return "u_" + SanitizeShaderIdentifier(StableParameterId(node), "parameter" + std::to_string(node.id)) + std::string{ suffix };
}

[[nodiscard]] std::string ConstantScalarExpression(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
    return FloatLiteral(values.empty() ? 1.0F : values[0]);
}

[[nodiscard]] std::string ConstantVectorExpression(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
    const float x = values.size() > 0U ? values[0] : 1.0F;
    const float y = values.size() > 1U ? values[1] : x;
    const float z = values.size() > 2U ? values[2] : y;
    return "vec3(" + FloatLiteral(x) + ", " + FloatLiteral(y) + ", " + FloatLiteral(z) + ")";
}

[[nodiscard]] std::string ConstantVector2Expression(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
    const float x = values.size() > 0U ? values[0] : 0.0F;
    const float y = values.size() > 1U ? values[1] : x;
    return "vec2(" + FloatLiteral(x) + ", " + FloatLiteral(y) + ")";
}

[[nodiscard]] std::string ConstantColorExpression(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
    const float r = values.size() > 0U ? values[0] : 1.0F;
    const float g = values.size() > 1U ? values[1] : r;
    const float b = values.size() > 2U ? values[2] : g;
    const float a = values.size() > 3U ? values[3] : 1.0F;
    return "vec4(" + FloatLiteral(r) + ", " + FloatLiteral(g) + ", " + FloatLiteral(b) + ", " + FloatLiteral(a) + ")";
}

[[nodiscard]] std::string CompileTextureInputExpression(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    const RenderMaterialGraphLink* link = FindInputLink(graph, node.id, "texture");
    if (link == nullptr) {
        return ParameterUniformName(node, "_texture");
    }
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link->fromNodeId);
    if (fromNode == nullptr || fromNode->kind != RenderMaterialGraphNodeKind::ParameterTexture || link->fromPin != "texture") {
        AddShaderGenerationDiagnostic(diagnostics, node, "texture", "TextureSample texture input must be connected to a texture parameter.");
        return "u_missingTexture";
    }
    return ParameterUniformName(*fromNode, "_texture");
}

// MAT-36: default-initialise every MaterialSurface channel so MakeMaterialAttributes / unconnected
// BreakMaterialAttributes produce a fully-defined struct (no undefined members).
[[nodiscard]] std::string MaterialSurfaceDefaultInitStatements(const std::string& v) {
    return
        "    " + v + ".baseColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
        "    " + v + ".metallic = 0.0;\n"
        "    " + v + ".roughness = 1.0;\n"
        "    " + v + ".normal = vec3(0.0, 0.0, 1.0);\n"
        "    " + v + ".occlusion = 1.0;\n"
        "    " + v + ".emissive = vec3(0.0, 0.0, 0.0);\n"
        "    " + v + ".alpha = 1.0;\n"
        "    " + v + ".alphaClipThreshold = 0.5;\n"
        "    " + v + ".specular = 0.5;\n"
        "    " + v + ".anisotropy = 0.0;\n"
        "    " + v + ".tangent = vec3(1.0, 0.0, 0.0);\n"
        "    " + v + ".subsurfaceColor = vec3(0.0, 0.0, 0.0);\n"
        "    " + v + ".clearCoat = 0.0;\n"
        "    " + v + ".clearCoatRoughness = 0.0;\n"
        "    " + v + ".refraction = 0.0;\n"
        "    " + v + ".surfaceThickness = 0.0;\n";
}

// MAT-39: parse a compile-time boolean from a node's value hint.
[[nodiscard]] bool ParseStaticBoolHint(std::string_view hint) noexcept {
    return EqualsIgnoreCase(hint, "true") || hint == "1";
}

// MAT-39: resolve a StaticSwitch's compile-time selector. It reads a linked StaticBoolParameter's value if
// present, otherwise the switch's own hint. The result is known at compile time, so only one branch is
// ever emitted and the choice is baked into the shader source (and therefore the variant key).
[[nodiscard]] bool ResolveStaticSwitchSelector(const RenderMaterialGraphDocument& graph, const RenderMaterialGraphNode& node) {
    if (const RenderMaterialGraphLink* link = FindInputLink(graph, node.id, "value")) {
        for (const RenderMaterialGraphNode& source : graph.nodes) {
            if (source.id == link->fromNodeId && source.kind == RenderMaterialGraphNodeKind::StaticBoolParameter) {
                return ParseStaticBoolHint(source.parameter.defaultValueHint);
            }
        }
    }
    return ParseStaticBoolHint(node.parameter.defaultValueHint);
}

// MAT-39: build the swizzle of a StaticComponentMask. Selected channels pass through; unselected channels
// are zeroed so the result stays a vec4 (a type-stable component mask). The mask string is compile-time.
[[nodiscard]] std::string StaticComponentMaskExpression(std::string_view source, std::string_view maskHint) {
    const std::string mask = maskHint.empty() ? std::string{ "rgba" } : std::string{ maskHint };
    const auto has = [&mask](char component) { return mask.find(component) != std::string::npos; };
    const std::string r = (has('r') || has('x')) ? "(" + std::string{ source } + ").x" : "0.0";
    const std::string g = (has('g') || has('y')) ? "(" + std::string{ source } + ").y" : "0.0";
    const std::string b = (has('b') || has('z')) ? "(" + std::string{ source } + ").z" : "0.0";
    const std::string a = (has('a') || has('w')) ? "(" + std::string{ source } + ").w" : "0.0";
    return "vec4(" + r + ", " + g + ", " + b + ", " + a + ")";
}

std::string CompileNodeBaseExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        // A compile-time boolean baked as a literal so it participates in the shader source / variant key.
        return ParseStaticBoolHint(node.parameter.defaultValueHint) ? "1.0" : "0.0";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        // Only the selected branch is compiled — the other subgraph is never emitted (dead-branch elimination).
        return ResolveStaticSwitchSelector(cg.graph, node)
            ? CompileInputExpression(cg, node, "true", RenderMaterialGraphPinType::Float4, "vec4(0.0)")
            : CompileInputExpression(cg, node, "false", RenderMaterialGraphPinType::Float4, "vec4(0.0)");
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return StaticComponentMaskExpression(
            CompileInputExpression(cg, node, "input", RenderMaterialGraphPinType::Float4, "vec4(0.0)"),
            node.parameter.defaultValueHint);
    case RenderMaterialGraphNodeKind::TextureCoordinate: {
        // MAT-45: tiling-scaled UV from a selectable coordinate set (hint = "uTile vTile [set]").
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float uTile = values.size() > 0U ? values[0] : 1.0F;
        const float vTile = values.size() > 1U ? values[1] : uTile;
        const bool useUv1 = values.size() > 2U && values[2] >= 0.5F;
        return std::string{ useUv1 ? "ctx.uv1" : "ctx.uv0" } + " * vec2(" + FloatLiteral(uTile) + ", " + FloatLiteral(vTile) + ")";
    }
    case RenderMaterialGraphNodeKind::ViewportUV:
        // MAT-45: the normalised viewport coordinate (same source as ScreenPosition).
        return "ctx.screenPosition";
    // MAT-46: world/object-space view inputs read from the shadow-safe context.
    case RenderMaterialGraphNodeKind::CameraPosition:
        return "ctx.cameraPosition";
    case RenderMaterialGraphNodeKind::CameraVector:
        return "ctx.viewDir";
    case RenderMaterialGraphNodeKind::ReflectionVector:
        return "reflect(-ctx.viewDir, ctx.normal)";
    case RenderMaterialGraphNodeKind::LightVector:
        return "ctx.lightVector";
    case RenderMaterialGraphNodeKind::PixelNormalWS:
    case RenderMaterialGraphNodeKind::VertexNormalWS:
        return "ctx.normal";
    case RenderMaterialGraphNodeKind::VertexTangentWS:
        return "ctx.tangent";
    case RenderMaterialGraphNodeKind::ViewSize:
    case RenderMaterialGraphNodeKind::ViewProperty:
        return "ctx.viewSize";
    case RenderMaterialGraphNodeKind::Panner: {
        // MAT-45: scroll the coordinate by time * speed (hint = "speedU speedV").
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float speedU = values.size() > 0U ? values[0] : 0.1F;
        const float speedV = values.size() > 1U ? values[1] : 0.0F;
        const std::string coord = CompileInputExpression(cg, node, "coordinate", RenderMaterialGraphPinType::Float2, "ctx.uv0");
        const std::string time = CompileInputExpression(cg, node, "time", RenderMaterialGraphPinType::Float, "ctx.time");
        return "((" + coord + ") + (" + time + ") * vec2(" + FloatLiteral(speedU) + ", " + FloatLiteral(speedV) + "))";
    }
    case RenderMaterialGraphNodeKind::BumpOffset: {
        // MAT-45: parallax-style UV offset along the tangent-space view direction (hint = "heightRatio").
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float ratio = values.size() > 0U ? values[0] : 0.05F;
        const std::string coord = CompileInputExpression(cg, node, "coordinate", RenderMaterialGraphPinType::Float2, "ctx.uv0");
        const std::string height = CompileInputExpression(cg, node, "height", RenderMaterialGraphPinType::Float, "0.0");
        return "((" + coord + ") + ((" + height + ") - 0.5) * " + FloatLiteral(ratio) + " * ctx.viewDir.xy)";
    }
    case RenderMaterialGraphNodeKind::ConstantBiasScale: {
        // MAT-45: (input + bias) * scale, applied per channel (hint = "bias scale").
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float bias = values.size() > 0U ? values[0] : 0.0F;
        const float scale = values.size() > 1U ? values[1] : 1.0F;
        const std::string input = CompileInputExpression(cg, node, "input", RenderMaterialGraphPinType::Float4, "vec4(0.0)");
        return "(((" + input + ") + vec4_splat(" + FloatLiteral(bias) + ")) * vec4_splat(" + FloatLiteral(scale) + "))";
    }
    case RenderMaterialGraphNodeKind::Rotator: {
        // MAT-45: rotate the coordinate about a centre by time * speed (hint = "speed centerU centerV").
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float speed = values.size() > 0U ? values[0] : 1.0F;
        const float centerU = values.size() > 1U ? values[1] : 0.5F;
        const float centerV = values.size() > 2U ? values[2] : 0.5F;
        const std::string coord = CompileInputExpression(cg, node, "coordinate", RenderMaterialGraphPinType::Float2, "ctx.uv0");
        const std::string time = CompileInputExpression(cg, node, "time", RenderMaterialGraphPinType::Float, "ctx.time");
        const std::string id = std::to_string(node.id);
        const std::string center = "vec2(" + FloatLiteral(centerU) + ", " + FloatLiteral(centerV) + ")";
        const std::string tmp = "rotUv" + id;
        cg.statements += "    float rotAngle" + id + " = (" + time + ") * " + FloatLiteral(speed) + ";\n";
        cg.statements += "    vec2 rotRel" + id + " = (" + coord + ") - " + center + ";\n";
        cg.statements += "    vec2 " + tmp + " = " + center + " + vec2(rotRel" + id + ".x * cos(rotAngle" + id + ") - rotRel" + id + ".y * sin(rotAngle" + id + "), rotRel" + id + ".x * sin(rotAngle" + id + ") + rotRel" + id + ".y * cos(rotAngle" + id + "));\n";
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::RotateAboutAxis: {
        // MAT-45: Rodrigues rotation of a position vector about an axis by an angle (radians).
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string axis = CompileInputExpression(cg, node, "axis", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const std::string angle = CompileInputExpression(cg, node, "angle", RenderMaterialGraphPinType::Float, "0.0");
        const std::string position = CompileInputExpression(cg, node, "position", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)");
        const std::string id = std::to_string(node.id);
        const std::string tmp = "rax" + id;
        cg.statements += "    vec3 raxAxis" + id + " = normalize(" + axis + ");\n";
        cg.statements += "    float raxCos" + id + " = cos(" + angle + ");\n";
        cg.statements += "    float raxSin" + id + " = sin(" + angle + ");\n";
        cg.statements += "    vec3 raxPos" + id + " = " + position + ";\n";
        cg.statements += "    vec3 " + tmp + " = raxPos" + id + " * raxCos" + id + " + cross(raxAxis" + id + ", raxPos" + id + ") * raxSin" + id + " + raxAxis" + id + " * dot(raxAxis" + id + ", raxPos" + id + ") * (1.0 - raxCos" + id + ");\n";
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes: {
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string tmp = "attrs" + std::to_string(node.id);
        cg.statements += "    MaterialSurface " + tmp + ";\n";
        cg.statements += MaterialSurfaceDefaultInitStatements(tmp);
        cg.statements += "    " + tmp + ".baseColor = " + CompileInputExpression(cg, node, "baseColor", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)") + ";\n";
        cg.statements += "    " + tmp + ".metallic = " + CompileInputExpression(cg, node, "metallic", RenderMaterialGraphPinType::Float, "0.0") + ";\n";
        cg.statements += "    " + tmp + ".roughness = " + CompileInputExpression(cg, node, "roughness", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        cg.statements += "    " + tmp + ".normal = " + CompileInputExpression(cg, node, "normal", RenderMaterialGraphPinType::Normal, "vec3(0.0, 0.0, 1.0)") + ";\n";
        cg.statements += "    " + tmp + ".emissive = " + CompileInputExpression(cg, node, "emissive", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)") + ".rgb;\n";
        cg.statements += "    " + tmp + ".occlusion = " + CompileInputExpression(cg, node, "occlusion", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        cg.statements += "    " + tmp + ".alpha = " + CompileInputExpression(cg, node, "alpha", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes: {
        if (FindInputLink(cg.graph, node.id, "attributes") != nullptr) {
            return CompileInputExpression(cg, node, "attributes", RenderMaterialGraphPinType::MaterialAttributes, "");
        }
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string tmp = "attrsDefault" + std::to_string(node.id);
        cg.statements += "    MaterialSurface " + tmp + ";\n";
        cg.statements += MaterialSurfaceDefaultInitStatements(tmp);
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes: {
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string a = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::MaterialAttributes, "");
        const std::string b = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::MaterialAttributes, "");
        const std::string f = CompileInputExpression(cg, node, "factor", RenderMaterialGraphPinType::Float, "0.5");
        const std::string tmp = "attrsBlend" + std::to_string(node.id);
        const std::string fv = "blendF" + std::to_string(node.id);
        cg.statements += "    float " + fv + " = clamp(" + f + ", 0.0, 1.0);\n";
        cg.statements += "    MaterialSurface " + tmp + ";\n";
        cg.statements += "    " + tmp + ".baseColor = mix((" + a + ").baseColor, (" + b + ").baseColor, " + fv + ");\n";
        cg.statements += "    " + tmp + ".metallic = mix((" + a + ").metallic, (" + b + ").metallic, " + fv + ");\n";
        cg.statements += "    " + tmp + ".roughness = mix((" + a + ").roughness, (" + b + ").roughness, " + fv + ");\n";
        cg.statements += "    " + tmp + ".normal = normalize(mix((" + a + ").normal, (" + b + ").normal, " + fv + "));\n";
        cg.statements += "    " + tmp + ".occlusion = mix((" + a + ").occlusion, (" + b + ").occlusion, " + fv + ");\n";
        cg.statements += "    " + tmp + ".emissive = mix((" + a + ").emissive, (" + b + ").emissive, " + fv + ");\n";
        cg.statements += "    " + tmp + ".alpha = mix((" + a + ").alpha, (" + b + ").alpha, " + fv + ");\n";
        cg.statements += "    " + tmp + ".alphaClipThreshold = mix((" + a + ").alphaClipThreshold, (" + b + ").alphaClipThreshold, " + fv + ");\n";
        cg.statements += "    " + tmp + ".specular = mix((" + a + ").specular, (" + b + ").specular, " + fv + ");\n";
        cg.statements += "    " + tmp + ".anisotropy = mix((" + a + ").anisotropy, (" + b + ").anisotropy, " + fv + ");\n";
        cg.statements += "    " + tmp + ".tangent = mix((" + a + ").tangent, (" + b + ").tangent, " + fv + ");\n";
        cg.statements += "    " + tmp + ".subsurfaceColor = mix((" + a + ").subsurfaceColor, (" + b + ").subsurfaceColor, " + fv + ");\n";
        cg.statements += "    " + tmp + ".clearCoat = mix((" + a + ").clearCoat, (" + b + ").clearCoat, " + fv + ");\n";
        cg.statements += "    " + tmp + ".clearCoatRoughness = mix((" + a + ").clearCoatRoughness, (" + b + ").clearCoatRoughness, " + fv + ");\n";
        cg.statements += "    " + tmp + ".refraction = mix((" + a + ").refraction, (" + b + ").refraction, " + fv + ");\n";
        cg.statements += "    " + tmp + ".surfaceThickness = mix((" + a + ").surfaceThickness, (" + b + ").surfaceThickness, " + fv + ");\n";
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::GetMaterialAttributes: {
        // GetMaterialAttributes reads channels out of an attribute set — the set itself is the source expression.
        if (FindInputLink(cg.graph, node.id, "attributes") != nullptr) {
            return CompileInputExpression(cg, node, "attributes", RenderMaterialGraphPinType::MaterialAttributes, "");
        }
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string tmp = "attrsGetDefault" + std::to_string(node.id);
        cg.statements += "    MaterialSurface " + tmp + ";\n";
        cg.statements += MaterialSurfaceDefaultInitStatements(tmp);
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::SetMaterialAttributes: {
        // SetMaterialAttributes overrides only the connected channels of an incoming set, leaving the rest intact.
        if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
            return it->second;
        }
        const std::string tmp = "attrsSet" + std::to_string(node.id);
        cg.statements += "    MaterialSurface " + tmp + ";\n";
        if (FindInputLink(cg.graph, node.id, "attributes") != nullptr) {
            cg.statements += "    " + tmp + " = " + CompileInputExpression(cg, node, "attributes", RenderMaterialGraphPinType::MaterialAttributes, "") + ";\n";
        } else {
            cg.statements += MaterialSurfaceDefaultInitStatements(tmp);
        }
        if (FindInputLink(cg.graph, node.id, "baseColor") != nullptr) {
            cg.statements += "    " + tmp + ".baseColor = " + CompileInputExpression(cg, node, "baseColor", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "metallic") != nullptr) {
            cg.statements += "    " + tmp + ".metallic = " + CompileInputExpression(cg, node, "metallic", RenderMaterialGraphPinType::Float, "0.0") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "roughness") != nullptr) {
            cg.statements += "    " + tmp + ".roughness = " + CompileInputExpression(cg, node, "roughness", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "normal") != nullptr) {
            cg.statements += "    " + tmp + ".normal = " + CompileInputExpression(cg, node, "normal", RenderMaterialGraphPinType::Normal, "vec3(0.0, 0.0, 1.0)") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "emissive") != nullptr) {
            cg.statements += "    " + tmp + ".emissive = " + CompileInputExpression(cg, node, "emissive", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)") + ".rgb;\n";
        }
        if (FindInputLink(cg.graph, node.id, "occlusion") != nullptr) {
            cg.statements += "    " + tmp + ".occlusion = " + CompileInputExpression(cg, node, "occlusion", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "alpha") != nullptr) {
            cg.statements += "    " + tmp + ".alpha = " + CompileInputExpression(cg, node, "alpha", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
        }
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return ConstantScalarExpression(node);
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return ConstantVector2Expression(node);
    case RenderMaterialGraphNodeKind::ConstantVector:
        return ConstantVectorExpression(node);
    case RenderMaterialGraphNodeKind::ConstantColor:
        return ConstantColorExpression(node);
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return ParameterUniformName(node, "") + ".x";
    case RenderMaterialGraphNodeKind::ParameterVector:
        return ParameterUniformName(node, "_xyz") + ".xyz";
    case RenderMaterialGraphNodeKind::ParameterColor:
        return ParameterUniformName(node, "_rgba");
    case RenderMaterialGraphNodeKind::Add:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            " + " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Subtract:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            " - " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Multiply:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            " * " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Divide:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            " / max(abs(" +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            "), vec4(0.0001)))";
    case RenderMaterialGraphNodeKind::Power:
        return "pow(max(" +
            CompileInputExpression(cg, node, "base", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", vec4(0.0)), " +
            CompileInputExpression(cg, node, "exponent", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::OneMinus:
        return "(vec4(1.0) - " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Absolute:
        return "abs(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Minimum:
        return "min(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Maximum:
        return "max(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Saturate:
        return "clamp(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", vec4(0.0), vec4(1.0))";
    case RenderMaterialGraphNodeKind::Floor:
        return "floor(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Ceil:
        return "ceil(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Fraction:
        return "fract(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::SquareRoot:
        return "sqrt(max(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", vec4(0.0)))";
    case RenderMaterialGraphNodeKind::Sine:
        return "sin(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Cosine:
        return "cos(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::DotProduct:
        return "dot(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::CrossProduct:
        return "cross(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 1.0, 0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Normalize: {
        const std::string vector = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        return "((length(" + vector + ") > 0.0001) ? normalize(" + vector + ") : vec3(0.0, 0.0, 1.0))";
    }
    case RenderMaterialGraphNodeKind::Length:
        return "length(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Distance:
        return "distance(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::BreakVector:
        return CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0, 0.0, 0.0, 1.0)");
    case RenderMaterialGraphNodeKind::MakeVector:
        return "vec4(" +
            CompileInputExpression(cg, node, "x", RenderMaterialGraphPinType::Float, "0.0") +
            ", " +
            CompileInputExpression(cg, node, "y", RenderMaterialGraphPinType::Float, "0.0") +
            ", " +
            CompileInputExpression(cg, node, "z", RenderMaterialGraphPinType::Float, "0.0") +
            ", " +
            CompileInputExpression(cg, node, "w", RenderMaterialGraphPinType::Float, "1.0") +
            ")";
    case RenderMaterialGraphNodeKind::Step:
        return "step(" +
            CompileInputExpression(cg, node, "edge", RenderMaterialGraphPinType::Float4, "vec4(0.5)") +
            ", " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::SmoothStep:
        return "smoothstep(" +
            CompileInputExpression(cg, node, "min", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "max", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ", " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::If: {
        const std::string lhs = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float, "0.0");
        const std::string rhs = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float, "0.0");
        const std::string less = CompileInputExpression(cg, node, "less", RenderMaterialGraphPinType::Float4, "vec4(0.0)");
        const std::string equal = CompileInputExpression(cg, node, "equal", RenderMaterialGraphPinType::Float4, "vec4(0.5)");
        const std::string greater = CompileInputExpression(cg, node, "greater", RenderMaterialGraphPinType::Float4, "vec4(1.0)");
        return "((" + lhs + " > " + rhs + ") ? " + greater + " : ((abs(" + lhs + " - " + rhs + ") <= 0.0001) ? " + equal + " : " + less + "))";
    }
    case RenderMaterialGraphNodeKind::Desaturate: {
        const std::string color = CompileInputExpression(cg, node, "color", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)");
        const std::string fraction = CompileInputExpression(cg, node, "fraction", RenderMaterialGraphPinType::Float, "1.0");
        const std::string luma = "dot((" + color + ").rgb, vec3(0.299, 0.587, 0.114))";
        return "mix(" + color + ", vec4(vec3(" + luma + "), (" + color + ").a), clamp(" + fraction + ", 0.0, 1.0))";
    }
    case RenderMaterialGraphNodeKind::Fresnel: {
        const std::string normal = CompileInputExpression(cg, node, "normal", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const std::string view = CompileInputExpression(cg, node, "view", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const std::string exponent = CompileInputExpression(cg, node, "exponent", RenderMaterialGraphPinType::Float, "5.0");
        const std::string base = CompileInputExpression(cg, node, "base", RenderMaterialGraphPinType::Float, "0.0");
        const std::string facing = "clamp(dot(normalize(" + normal + "), normalize(" + view + ")), 0.0, 1.0)";
        return "mix(pow(1.0 - " + facing + ", max(" + exponent + ", 0.0001)), 1.0, clamp(" + base + ", 0.0, 1.0))";
    }
    case RenderMaterialGraphNodeKind::Negate:
        return "-(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Sign:
        return "sign(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Round:
        return "round(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Truncate: {
        const std::string value = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)");
        return "sign(" + value + ") * floor(abs(" + value + "))";
    }
    case RenderMaterialGraphNodeKind::Tangent:
        return "tan(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ")";
    case RenderMaterialGraphNodeKind::ArcSine:
        return "asin(clamp(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ", vec4(-1.0), vec4(1.0)))";
    case RenderMaterialGraphNodeKind::ArcCosine:
        return "acos(clamp(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(1.0)") + ", vec4(-1.0), vec4(1.0)))";
    case RenderMaterialGraphNodeKind::ArcTangent:
        return "atan(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") + ")";
    case RenderMaterialGraphNodeKind::ArcTangent2:
        return "atan(" +
            CompileInputExpression(cg, node, "y", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "x", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Clamp:
        return "clamp(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "min", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "max", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Lerp:
        return "mix(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)") +
            ", " +
            CompileInputExpression(cg, node, "t", RenderMaterialGraphPinType::Float, "0.0") +
            ")";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return "normalize((" +
            CompileInputExpression(cg, node, "color", RenderMaterialGraphPinType::Color, "vec4(0.5, 0.5, 1.0, 1.0)") +
            ").rgb * 2.0 - vec3(1.0, 1.0, 1.0))";
    case RenderMaterialGraphNodeKind::Uv:
        // uvSet selector via the node value hint: "1"/"uv1" -> second UV set (MAT-73), else uv0.
        return (node.parameter.defaultValueHint == "1" || node.parameter.defaultValueHint == "uv1")
            ? "ctx.uv1"
            : "ctx.uv0";
    case RenderMaterialGraphNodeKind::Time:
        return "ctx.time";
    case RenderMaterialGraphNodeKind::VertexColor:
        return "ctx.vertexColor";
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return "ctx.screenPosition";
    case RenderMaterialGraphNodeKind::LocalPosition:
        return "ctx.localPosition";
    case RenderMaterialGraphNodeKind::ObjectPosition:
        return "ctx.objectPosition";
    case RenderMaterialGraphNodeKind::WorldPosition:
        return "ctx.worldPos";
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
        return "ctx.perInstanceRandom";
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return "ctx.objectRadius";
    case RenderMaterialGraphNodeKind::TextureSample:
        return "texture2D(" +
            CompileTextureInputExpression(cg.graph, node, cg.diagnostics) +
            ", " +
            CompileInputExpression(cg, node, "uv", RenderMaterialGraphPinType::Float2, "ctx.uv0") +
            ")";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, "texture", "Texture parameter cannot be emitted as a numeric shader expression without a TextureSample node.");
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node.kind, "texture", true));
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return DefaultExpressionForType(RenderMaterialGraphPinType::Unknown);
    }
    return DefaultExpressionForType(RenderMaterialGraphPinType::Unknown);
}

std::string SelectGraphPinFromBase(GraphCodegen& cg, const RenderMaterialGraphNode& node, const std::string& baseRef, std::string_view outputPin) {
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        if (outputPin == "baseColor") return "(" + baseRef + ").baseColor";
        if (outputPin == "metallic") return "(" + baseRef + ").metallic";
        if (outputPin == "roughness") return "(" + baseRef + ").roughness";
        if (outputPin == "normal") return "(" + baseRef + ").normal";
        if (outputPin == "emissive") return "vec4((" + baseRef + ").emissive, 1.0)";
        if (outputPin == "occlusion") return "(" + baseRef + ").occlusion";
        if (outputPin == "alpha") return "(" + baseRef + ").alpha";
        AddShaderGenerationDiagnostic(cg.diagnostics, node, outputPin, "Material attribute output pin is not supported.");
        return "0.0";
    case RenderMaterialGraphNodeKind::BreakVector:
        if (outputPin == "x") {
            return "(" + baseRef + ").x";
        }
        if (outputPin == "y") {
            return "(" + baseRef + ").y";
        }
        if (outputPin == "z") {
            return "(" + baseRef + ").z";
        }
        if (outputPin == "w") {
            return "(" + baseRef + ").w";
        }
        AddShaderGenerationDiagnostic(cg.diagnostics, node, outputPin, "BreakVector output pin is not supported.");
        return "0.0";
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::VertexColor:
        if (outputPin == "r" || outputPin == "g" || outputPin == "b" || outputPin == "a") {
            return "(" + baseRef + ")." + std::string{ outputPin };
        }
        return baseRef;
    default:
        return baseRef;
    }
}

std::string CompileNodeOutputExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node, std::string_view outputPin) {
    if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
        return SelectGraphPinFromBase(cg, node, it->second, outputPin);
    }
    if (ContainsNode(cg.stack, node.id)) {
        AddShaderGenerationDiagnostic(cg.diagnostics, node, outputPin, "Material graph shader generation hit a recursive node dependency.");
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node.kind, outputPin, true));
    }

    cg.stack.push_back(node.id);
    std::string base = CompileNodeBaseExpression(cg, node);
    cg.stack.pop_back();

    const auto fanOutIt = cg.fanOut.find(node.id);
    const std::uint32_t fanOut = fanOutIt == cg.fanOut.end() ? 0U : fanOutIt->second;
    if (IsGraphCseCandidate(node.kind) && fanOut > 1U) {
        std::string tempName = "n" + std::to_string(node.id) + "_v";
        cg.statements += "    " + GraphCodegenGlslType(GraphNodeCanonicalType(node.kind)) + " " + tempName + " = " + base + ";\n";
        cg.emittedTemp.emplace(node.id, tempName);
        return SelectGraphPinFromBase(cg, node, tempName, outputPin);
    }
    return SelectGraphPinFromBase(cg, node, base, outputPin);
}

[[nodiscard]] std::string DefaultStableParameterId(const RenderMaterialGraphNode& node) {
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
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        break;
    }
    return "parameter" + std::to_string(node.id);
}

[[nodiscard]] std::string StableParameterId(const RenderMaterialGraphNode& node) {
    return node.parameter.stableId.empty() ? DefaultStableParameterId(node) : node.parameter.stableId;
}

[[nodiscard]] std::optional<RenderMaterialTextureColorSpace> ExpectedColorSpaceForTextureRole(std::string_view role) noexcept;

[[nodiscard]] std::string DisplayNameForParameter(const RenderMaterialGraphNode& node) {
    return node.parameter.displayName.empty() ? StableParameterId(node) : node.parameter.displayName;
}

[[nodiscard]] std::string EffectiveTextureRoleForNode(const RenderMaterialGraphNode& node) {
    if (!node.parameter.textureRole.empty()) {
        return node.parameter.textureRole;
    }
    return node.kind == RenderMaterialGraphNodeKind::TextureSample ? "baseColor" : std::string{};
}

[[nodiscard]] RenderMaterialTextureColorSpace EffectiveTextureColorSpaceForNode(
    const RenderMaterialGraphNode& node,
    std::string_view role) noexcept {
    if (node.parameter.expectedTextureColorSpace != RenderMaterialTextureColorSpace::Unknown) {
        return node.parameter.expectedTextureColorSpace;
    }
    if (node.kind == RenderMaterialGraphNodeKind::TextureSample) {
        return ExpectedColorSpaceForTextureRole(role).value_or(RenderMaterialTextureColorSpace::Srgb);
    }
    return RenderMaterialTextureColorSpace::Unknown;
}

[[nodiscard]] std::string TextureAssetFieldName(const std::string& stableId) {
    return stableId + "TextureAssetId";
}

[[nodiscard]] std::string TexturePathFieldName(const std::string& stableId) {
    return stableId + "Texture";
}

[[nodiscard]] RenderMaterialParameterType ParameterTypeForNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return RenderMaterialParameterType::Scalar;
    case RenderMaterialGraphNodeKind::ParameterVector:
        return RenderMaterialParameterType::Vec3;
    case RenderMaterialGraphNodeKind::ParameterColor:
        return RenderMaterialParameterType::Color;
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return RenderMaterialParameterType::Texture;
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        break;
    }
    return RenderMaterialParameterType::Scalar;
}

[[nodiscard]] bool IsKnownNodeKind(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        return true;
    }
    return false;
}

[[nodiscard]] bool IsSingleOutputOnlyGraph(const RenderMaterialGraphDocument& graph) noexcept {
    return graph.nodes.size() == 1U &&
        graph.links.empty() &&
        graph.nodes[0].kind == RenderMaterialGraphNodeKind::MaterialOutput;
}

[[nodiscard]] bool HasInputLink(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    return std::any_of(graph.links.begin(), graph.links.end(), [nodeId, pin](const RenderMaterialGraphLink& link) {
        return link.toNodeId == nodeId && link.toPin == pin;
    });
}

[[nodiscard]] std::optional<RenderMaterialTextureColorSpace> ExpectedColorSpaceForTextureRole(std::string_view role) noexcept {
    if (role == "baseColor" || role == "albedo" || role == "emissive") {
        return RenderMaterialTextureColorSpace::Srgb;
    }
    if (role == "normal" || role == "metallicRoughness" || role == "occlusion") {
        return RenderMaterialTextureColorSpace::Linear;
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t GraphNodeStateIndex(
    const std::vector<std::pair<std::uint32_t, std::uint8_t>>& states,
    std::uint32_t nodeId) noexcept {
    for (std::size_t index = 0U; index < states.size(); ++index) {
        if (states[index].first == nodeId) {
            return index;
        }
    }
    return states.size();
}

[[nodiscard]] bool VisitGraphNodeForCycle(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::vector<std::pair<std::uint32_t, std::uint8_t>>& states) {
    const std::size_t stateIndex = GraphNodeStateIndex(states, nodeId);
    if (stateIndex == states.size()) {
        return false;
    }
    if (states[stateIndex].second == 1U) {
        return true;
    }
    if (states[stateIndex].second == 2U) {
        return false;
    }

    states[stateIndex].second = 1U;
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.fromNodeId != nodeId || FindRenderMaterialGraphNode(graph, link.toNodeId) == nullptr) {
            continue;
        }
        if (VisitGraphNodeForCycle(graph, link.toNodeId, states)) {
            return true;
        }
    }
    states[stateIndex].second = 2U;
    return false;
}

[[nodiscard]] bool GraphHasCycle(const RenderMaterialGraphDocument& graph) {
    std::vector<std::pair<std::uint32_t, std::uint8_t>> states;
    states.reserve(graph.nodes.size());
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        states.push_back({ node.id, static_cast<std::uint8_t>(0U) });
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (VisitGraphNodeForCycle(graph, node.id, states)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::string> RenderMaterialGraphNodeInputPinNames(RenderMaterialGraphNodeKind kind) {
    RenderMaterialGraphIrNode irNode{ .kind = kind };
    AppendIrPins(irNode);
    std::vector<std::string> names;
    names.reserve(irNode.inputs.size());
    for (const RenderMaterialGraphIrPin& pin : irNode.inputs) {
        names.push_back(pin.name);
    }
    return names;
}

std::vector<std::string> RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind kind) {
    RenderMaterialGraphIrNode irNode{ .kind = kind };
    AppendIrPins(irNode);
    std::vector<std::string> names;
    names.reserve(irNode.outputs.size());
    for (const RenderMaterialGraphIrPin& pin : irNode.outputs) {
        names.push_back(pin.name);
    }
    return names;
}

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
    case RenderMaterialGraphNodeKind::TextureSample:
        return "TextureSample";
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "ParameterScalar";
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "ParameterVector";
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "ParameterColor";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "ParameterTexture";
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
    case RenderMaterialGraphNodeKind::VertexColor:
        return "Vertex Color";
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return "Screen Position";
    case RenderMaterialGraphNodeKind::LocalPosition:
        return "Local Position";
    case RenderMaterialGraphNodeKind::ObjectPosition:
        return "Object Position";
    case RenderMaterialGraphNodeKind::WorldPosition:
        return "World Position";
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
        return "Per Instance Random";
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return "Object Radius";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return "Make Material Attributes";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return "Break Material Attributes";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return "Blend Material Attributes";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return "Get Material Attributes";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return "Set Material Attributes";
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return "Static Bool Parameter";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return "Static Switch";
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return "Static Component Mask";
    case RenderMaterialGraphNodeKind::TextureCoordinate:
        return "Texture Coordinate";
    case RenderMaterialGraphNodeKind::Panner:
        return "Panner";
    case RenderMaterialGraphNodeKind::Rotator:
        return "Rotator";
    case RenderMaterialGraphNodeKind::BumpOffset:
        return "Bump Offset";
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        return "Constant Bias Scale";
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        return "Rotate About Axis";
    case RenderMaterialGraphNodeKind::ViewportUV:
        return "Viewport UV";
    case RenderMaterialGraphNodeKind::CameraPosition:
        return "Camera Position";
    case RenderMaterialGraphNodeKind::CameraVector:
        return "Camera Vector";
    case RenderMaterialGraphNodeKind::ReflectionVector:
        return "Reflection Vector";
    case RenderMaterialGraphNodeKind::LightVector:
        return "Light Vector";
    case RenderMaterialGraphNodeKind::PixelNormalWS:
        return "Pixel Normal WS";
    case RenderMaterialGraphNodeKind::VertexNormalWS:
        return "Vertex Normal WS";
    case RenderMaterialGraphNodeKind::VertexTangentWS:
        return "Vertex Tangent WS";
    case RenderMaterialGraphNodeKind::ViewProperty:
        return "View Property";
    case RenderMaterialGraphNodeKind::ViewSize:
        return "View Size";
    }
    return "MaterialOutput";
}

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
    if (EqualsIgnoreCase(text, "TextureSample")) {
        return RenderMaterialGraphNodeKind::TextureSample;
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
    if (EqualsIgnoreCase(text, "BreakVector") || EqualsIgnoreCase(text, "BreakOutFloat4Components") || EqualsIgnoreCase(text, "ComponentMask")) {
        return RenderMaterialGraphNodeKind::BreakVector;
    }
    if (EqualsIgnoreCase(text, "MakeVector") || EqualsIgnoreCase(text, "MakeFloat4") || EqualsIgnoreCase(text, "AppendVector")) {
        return RenderMaterialGraphNodeKind::MakeVector;
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
    if (EqualsIgnoreCase(text, "Clamp")) {
        return RenderMaterialGraphNodeKind::Clamp;
    }
    if (EqualsIgnoreCase(text, "Lerp")) {
        return RenderMaterialGraphNodeKind::Lerp;
    }
    if (EqualsIgnoreCase(text, "NormalUnpack")) {
        return RenderMaterialGraphNodeKind::NormalUnpack;
    }
    if (EqualsIgnoreCase(text, "UV") || EqualsIgnoreCase(text, "Uv") || EqualsIgnoreCase(text, "TextureCoordinate")) {
        return RenderMaterialGraphNodeKind::Uv;
    }
    if (EqualsIgnoreCase(text, "Time")) {
        return RenderMaterialGraphNodeKind::Time;
    }
    if (EqualsIgnoreCase(text, "VertexColor")) {
        return RenderMaterialGraphNodeKind::VertexColor;
    }
    if (EqualsIgnoreCase(text, "ScreenPosition")) {
        return RenderMaterialGraphNodeKind::ScreenPosition;
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
    if (EqualsIgnoreCase(text, "ObjectRadius")) {
        return RenderMaterialGraphNodeKind::ObjectRadius;
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
    if (EqualsIgnoreCase(text, "StaticSwitch")) {
        return RenderMaterialGraphNodeKind::StaticSwitch;
    }
    if (EqualsIgnoreCase(text, "StaticComponentMask")) {
        return RenderMaterialGraphNodeKind::StaticComponentMask;
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
    return std::nullopt;
}

std::string_view RenderMaterialGraphArtifactFailurePolicyName(RenderMaterialGraphArtifactFailurePolicy policy) noexcept {
    switch (policy) {
    case RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial:
        return "LastGoodThenErrorMaterial";
    case RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial:
        return "ErrorMaterial";
    }
    return "LastGoodThenErrorMaterial";
}

std::optional<RenderMaterialGraphArtifactFailurePolicy> ParseRenderMaterialGraphArtifactFailurePolicy(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "LastGoodThenErrorMaterial") || EqualsIgnoreCase(text, "LastGoodThenError")) {
        return RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial;
    }
    if (EqualsIgnoreCase(text, "ErrorMaterial")) {
        return RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial;
    }
    return std::nullopt;
}

std::string_view RenderMaterialGraphDiagnosticKindName(RenderMaterialGraphDiagnosticKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput:
        return "disconnected_required_output";
    case RenderMaterialGraphDiagnosticKind::TypeMismatch:
        return "type_mismatch";
    case RenderMaterialGraphDiagnosticKind::Cycle:
        return "cycle";
    case RenderMaterialGraphDiagnosticKind::UnsupportedNode:
        return "unsupported_node";
    case RenderMaterialGraphDiagnosticKind::MissingTexture:
        return "missing_texture";
    case RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole:
        return "invalid_color_space_role";
    case RenderMaterialGraphDiagnosticKind::UnsupportedBlendMode:
        return "unsupported_blend_mode";
    case RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed:
        return "shader_generation_failed";
    case RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId:
        return "duplicate_parameter_stable_id";
    case RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode:
        return "unsupported_render_path_node";
    }
    return "unsupported_node";
}

std::string_view RenderMaterialGraphRenderPathName(RenderMaterialGraphRenderPath path) noexcept {
    switch (path) {
    case RenderMaterialGraphRenderPath::GpuForward:
        return "GpuForward";
    case RenderMaterialGraphRenderPath::GpuShadow:
        return "GpuShadow";
    case RenderMaterialGraphRenderPath::GpuDeferred:
        return "GpuDeferred";
    case RenderMaterialGraphRenderPath::CpuFallback:
        return "CpuFallback";
    case RenderMaterialGraphRenderPath::Preview:
        return "Preview";
    }
    return "GpuForward";
}

std::string_view RenderMaterialGraphNodeSupportName(RenderMaterialGraphNodeSupport support) noexcept {
    switch (support) {
    case RenderMaterialGraphNodeSupport::Production:
        return "Production";
    case RenderMaterialGraphNodeSupport::Experimental:
        return "Experimental";
    case RenderMaterialGraphNodeSupport::FallbackOnly:
        return "FallbackOnly";
    case RenderMaterialGraphNodeSupport::Unsupported:
        return "Unsupported";
    }
    return "Unsupported";
}

RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportStatus(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        return RenderMaterialGraphNodeSupport::Production;
    }
    return RenderMaterialGraphNodeSupport::Unsupported;
}

bool IsRenderMaterialGraphRenderPathProduction(RenderMaterialGraphRenderPath path) noexcept {
    switch (path) {
    case RenderMaterialGraphRenderPath::GpuForward:
    case RenderMaterialGraphRenderPath::GpuShadow:
    case RenderMaterialGraphRenderPath::CpuFallback:
    case RenderMaterialGraphRenderPath::Preview:
        return true;
    case RenderMaterialGraphRenderPath::GpuDeferred:
        return false;
    }
    return false;
}

RenderMaterialDomain ParseRenderMaterialDomain(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "surface")) return RenderMaterialDomain::Surface;
    if (EqualsIgnoreCase(text, "deferredDecal") || EqualsIgnoreCase(text, "deferred_decal") || EqualsIgnoreCase(text, "deferreddecal")) return RenderMaterialDomain::DeferredDecal;
    if (EqualsIgnoreCase(text, "lightFunction") || EqualsIgnoreCase(text, "light_function") || EqualsIgnoreCase(text, "lightfunction")) return RenderMaterialDomain::LightFunction;
    if (EqualsIgnoreCase(text, "volume")) return RenderMaterialDomain::Volume;
    if (EqualsIgnoreCase(text, "postProcess") || EqualsIgnoreCase(text, "post_process") || EqualsIgnoreCase(text, "postprocess")) return RenderMaterialDomain::PostProcess;
    if (EqualsIgnoreCase(text, "userInterface") || EqualsIgnoreCase(text, "user_interface") || EqualsIgnoreCase(text, "userinterface") || EqualsIgnoreCase(text, "ui")) return RenderMaterialDomain::UserInterface;
    return RenderMaterialDomain::Surface;
}

std::string_view RenderMaterialDomainName(RenderMaterialDomain domain) noexcept {
    switch (domain) {
    case RenderMaterialDomain::Surface: return "surface";
    case RenderMaterialDomain::DeferredDecal: return "deferredDecal";
    case RenderMaterialDomain::LightFunction: return "lightFunction";
    case RenderMaterialDomain::Volume: return "volume";
    case RenderMaterialDomain::PostProcess: return "postProcess";
    case RenderMaterialDomain::UserInterface: return "userInterface";
    }
    return "surface";
}

RenderMaterialShadingModel ParseRenderMaterialShadingModel(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "unlit")) return RenderMaterialShadingModel::Unlit;
    if (EqualsIgnoreCase(text, "lit") || EqualsIgnoreCase(text, "defaultLit") || EqualsIgnoreCase(text, "default_lit") || EqualsIgnoreCase(text, "defaultlit")) return RenderMaterialShadingModel::DefaultLit;
    if (EqualsIgnoreCase(text, "subsurface")) return RenderMaterialShadingModel::Subsurface;
    if (EqualsIgnoreCase(text, "clearCoat") || EqualsIgnoreCase(text, "clear_coat") || EqualsIgnoreCase(text, "clearcoat")) return RenderMaterialShadingModel::ClearCoat;
    if (EqualsIgnoreCase(text, "cloth")) return RenderMaterialShadingModel::Cloth;
    if (EqualsIgnoreCase(text, "hair")) return RenderMaterialShadingModel::Hair;
    if (EqualsIgnoreCase(text, "eye")) return RenderMaterialShadingModel::Eye;
    if (EqualsIgnoreCase(text, "singleLayerWater") || EqualsIgnoreCase(text, "single_layer_water") || EqualsIgnoreCase(text, "singlelayerwater")) return RenderMaterialShadingModel::SingleLayerWater;
    if (EqualsIgnoreCase(text, "thinTranslucent") || EqualsIgnoreCase(text, "thin_translucent") || EqualsIgnoreCase(text, "thintranslucent")) return RenderMaterialShadingModel::ThinTranslucent;
    return RenderMaterialShadingModel::DefaultLit;
}

std::string_view RenderMaterialShadingModelName(RenderMaterialShadingModel model) noexcept {
    switch (model) {
    case RenderMaterialShadingModel::Unlit: return "unlit";
    case RenderMaterialShadingModel::DefaultLit: return "defaultLit";
    case RenderMaterialShadingModel::Subsurface: return "subsurface";
    case RenderMaterialShadingModel::ClearCoat: return "clearCoat";
    case RenderMaterialShadingModel::Cloth: return "cloth";
    case RenderMaterialShadingModel::Hair: return "hair";
    case RenderMaterialShadingModel::Eye: return "eye";
    case RenderMaterialShadingModel::SingleLayerWater: return "singleLayerWater";
    case RenderMaterialShadingModel::ThinTranslucent: return "thinTranslucent";
    }
    return "defaultLit";
}

RenderMaterialGraphBlendMode ParseRenderMaterialGraphBlendMode(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "opaque")) return RenderMaterialGraphBlendMode::Opaque;
    if (EqualsIgnoreCase(text, "masked") || EqualsIgnoreCase(text, "mask")) return RenderMaterialGraphBlendMode::Masked;
    if (EqualsIgnoreCase(text, "translucent") || EqualsIgnoreCase(text, "transparent") || EqualsIgnoreCase(text, "alpha")) return RenderMaterialGraphBlendMode::Translucent;
    if (EqualsIgnoreCase(text, "additive")) return RenderMaterialGraphBlendMode::Additive;
    if (EqualsIgnoreCase(text, "modulate")) return RenderMaterialGraphBlendMode::Modulate;
    if (EqualsIgnoreCase(text, "alphaComposite") || EqualsIgnoreCase(text, "alpha_composite") || EqualsIgnoreCase(text, "alphacomposite") || EqualsIgnoreCase(text, "premultipliedAlpha") || EqualsIgnoreCase(text, "premultiplied_alpha")) return RenderMaterialGraphBlendMode::AlphaComposite;
    if (EqualsIgnoreCase(text, "alphaHoldout") || EqualsIgnoreCase(text, "alpha_holdout") || EqualsIgnoreCase(text, "alphaholdout")) return RenderMaterialGraphBlendMode::AlphaHoldout;
    return RenderMaterialGraphBlendMode::Opaque;
}

std::string_view RenderMaterialGraphBlendModeName(RenderMaterialGraphBlendMode mode) noexcept {
    switch (mode) {
    case RenderMaterialGraphBlendMode::Opaque: return "opaque";
    case RenderMaterialGraphBlendMode::Masked: return "masked";
    case RenderMaterialGraphBlendMode::Translucent: return "translucent";
    case RenderMaterialGraphBlendMode::Additive: return "additive";
    case RenderMaterialGraphBlendMode::Modulate: return "modulate";
    case RenderMaterialGraphBlendMode::AlphaComposite: return "alphaComposite";
    case RenderMaterialGraphBlendMode::AlphaHoldout: return "alphaHoldout";
    }
    return "opaque";
}

RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind kind, RenderMaterialGraphRenderPath path) noexcept {
    const RenderMaterialGraphNodeSupport status = RenderMaterialGraphNodeSupportStatus(kind);
    if (status == RenderMaterialGraphNodeSupport::Unsupported) {
        return RenderMaterialGraphNodeSupport::Unsupported;
    }
    if (!IsRenderMaterialGraphRenderPathProduction(path)) {
        return RenderMaterialGraphNodeSupport::FallbackOnly;
    }
    return status;
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
        RenderMaterialGraphNodeKind::ParameterScalar,
        RenderMaterialGraphNodeKind::ParameterVector,
        RenderMaterialGraphNodeKind::ParameterColor,
        RenderMaterialGraphNodeKind::ParameterTexture,
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
        RenderMaterialGraphNodeKind::ConstantVector2,
        RenderMaterialGraphNodeKind::Time,
        RenderMaterialGraphNodeKind::VertexColor,
        RenderMaterialGraphNodeKind::ScreenPosition,
        RenderMaterialGraphNodeKind::LocalPosition,
        RenderMaterialGraphNodeKind::ObjectPosition,
        RenderMaterialGraphNodeKind::WorldPosition,
        RenderMaterialGraphNodeKind::PerInstanceRandom,
        RenderMaterialGraphNodeKind::ObjectRadius,
        RenderMaterialGraphNodeKind::MakeMaterialAttributes,
        RenderMaterialGraphNodeKind::BreakMaterialAttributes,
        RenderMaterialGraphNodeKind::BlendMaterialAttributes,
        RenderMaterialGraphNodeKind::GetMaterialAttributes,
        RenderMaterialGraphNodeKind::SetMaterialAttributes,
        RenderMaterialGraphNodeKind::StaticBoolParameter,
        RenderMaterialGraphNodeKind::StaticSwitch,
        RenderMaterialGraphNodeKind::StaticComponentMask,
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

RenderMaterialGraphDocument MakeDefaultRenderMaterialGraphDocument() {
    RenderMaterialGraphDocument graph{};
    graph.documentVersion = kRenderMaterialGraphDocumentVersion;
    graph.hasExplicitDocumentVersion = true;
    graph.hasExplicitArtifactFailurePolicy = true;
    graph.materialDomain = "surface";
    graph.shadingModel = "lit";
    graph.storageModel = "inline-kbmat";
    graph.diagnosticSchemaVersion = 1U;
    graph.persistCompileDiagnostics = true;
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 640,
        .positionY = 240,
    });
    return graph;
}

void WriteRenderMaterialGraphDocument(std::ostream& output, const RenderMaterialGraphDocument& graph) {
    output << "graphVersion " << (graph.documentVersion == 0U ? kRenderMaterialGraphDocumentVersion : graph.documentVersion) << '\n';
    output << "graphMaterialDomain " << (graph.materialDomain.empty() ? "surface" : graph.materialDomain) << '\n';
    output << "graphShadingModel " << (graph.shadingModel.empty() ? "lit" : graph.shadingModel) << '\n';
    output << "graphBlendMode " << (graph.blendMode.empty() ? "opaque" : graph.blendMode) << '\n';
    output << "graphStorageModel " << (graph.storageModel.empty() ? "inline-kbmat" : graph.storageModel) << '\n';
    output << "graphDiagnosticSchemaVersion " << (graph.diagnosticSchemaVersion == 0U ? 1U : graph.diagnosticSchemaVersion) << '\n';
    output << "graphPersistCompileDiagnostics " << (graph.persistCompileDiagnostics ? "true" : "false") << '\n';
    output << "graphArtifactFailurePolicy " << RenderMaterialGraphArtifactFailurePolicyName(graph.artifactFailurePolicy) << '\n';
    if (graph.lastGoodArtifact.assetId != 0U) {
        output << "graphLastGoodArtifactAssetId " << graph.lastGoodArtifact.assetId << '\n';
    }
    if (graph.lastGoodArtifact.contentHash != 0U) {
        output << "graphLastGoodArtifactHash " << graph.lastGoodArtifact.contentHash << '\n';
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        output << "graphNode "
            << node.id << ' '
            << RenderMaterialGraphNodeKindName(node.kind) << ' '
            << node.positionX << ' '
            << node.positionY << '\n';
        if (ShouldPersistGraphNodeMetadata(node)) {
            output << "graphParameter "
                << node.id << ' '
                << (node.parameter.stableId.empty() ? "_" : EncodeToken(node.parameter.stableId)) << ' '
                << (node.parameter.displayName.empty() ? "_" : EncodeToken(node.parameter.displayName)) << ' '
                << ParameterGroupName(node.parameter.group) << ' '
                << (node.parameter.defaultValueHint.empty() ? "_" : EncodeToken(node.parameter.defaultValueHint)) << ' '
                << (node.parameter.hasRange ? std::to_string(node.parameter.rangeMin) : "_") << ' '
                << (node.parameter.hasRange ? std::to_string(node.parameter.rangeMax) : "_") << ' '
                << (node.parameter.textureRole.empty() ? "_" : EncodeToken(node.parameter.textureRole)) << ' '
                << TextureColorSpaceName(node.parameter.expectedTextureColorSpace) << ' '
                << (node.parameter.overrideSupported ? "true" : "false") << ' '
                << node.parameter.editorOrder << ' '
                << (node.parameter.description.empty() ? "_" : EncodeToken(node.parameter.description)) << '\n';
        }
    }
    for (const RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        const std::uint32_t fromPinId = link.fromPinId != 0U || fromNode == nullptr
            ? link.fromPinId
            : RenderMaterialGraphStablePinId(fromNode->kind, link.fromPin, true);
        const std::uint32_t toPinId = link.toPinId != 0U || toNode == nullptr
            ? link.toPinId
            : RenderMaterialGraphStablePinId(toNode->kind, link.toPin, false);
        const std::uint32_t linkId = link.id != 0U
            ? link.id
            : MakeRenderMaterialGraphLinkId(RenderMaterialGraphLink{
                .fromNodeId = link.fromNodeId,
                .fromPinId = fromPinId,
                .toNodeId = link.toNodeId,
                .toPinId = toPinId,
            });
        output << "graphLink "
            << linkId << ' '
            << link.fromNodeId << ' '
            << fromPinId << ' '
            << link.fromPin << ' '
            << link.toNodeId << ' '
            << toPinId << ' '
            << link.toPin << '\n';
    }
}

bool RenderMaterialGraphIrBuildResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

bool RenderMaterialGraphCompileResult::Succeeded() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

bool RenderMaterialGraphCompileArtifactCacheKey::operator==(const RenderMaterialGraphCompileArtifactCacheKey& rhs) const noexcept {
    return graphContentHash == rhs.graphContentHash &&
        dependencyHash == rhs.dependencyHash &&
        shaderIncludeHash == rhs.shaderIncludeHash &&
        combinedHash == rhs.combinedHash;
}

bool RenderMaterialGraphMaterialTypeBuildResult::Succeeded() const noexcept {
    return document.has_value() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

const RenderMaterialGraphCompileArtifact* RenderMaterialGraphCompileArtifactCache::Find(const RenderMaterialGraphCompileArtifactCacheKey& key) const noexcept {
    for (const RenderMaterialGraphCompileArtifact& artifact : artifacts_) {
        if (artifact.key == key) {
            return &artifact;
        }
    }
    return nullptr;
}

void RenderMaterialGraphCompileArtifactCache::Store(RenderMaterialGraphCompileArtifact artifact) {
    for (RenderMaterialGraphCompileArtifact& existing : artifacts_) {
        if (existing.key == artifact.key) {
            existing = std::move(artifact);
            return;
        }
    }
    artifacts_.push_back(std::move(artifact));
}

bool RenderMaterialGraphCompileArtifactCache::Invalidate(const RenderMaterialGraphCompileArtifactCacheKey& key) {
    const auto oldEnd = std::remove_if(artifacts_.begin(), artifacts_.end(), [&key](const RenderMaterialGraphCompileArtifact& artifact) {
        return artifact.key == key;
    });
    if (oldEnd == artifacts_.end()) {
        return false;
    }
    artifacts_.erase(oldEnd, artifacts_.end());
    return true;
}

std::size_t RenderMaterialGraphCompileArtifactCache::InvalidateGraphContentHash(std::uint64_t graphContentHash) {
    const std::size_t before = artifacts_.size();
    const auto oldEnd = std::remove_if(artifacts_.begin(), artifacts_.end(), [graphContentHash](const RenderMaterialGraphCompileArtifact& artifact) {
        return artifact.key.graphContentHash == graphContentHash;
    });
    artifacts_.erase(oldEnd, artifacts_.end());
    return before - artifacts_.size();
}

void RenderMaterialGraphCompileArtifactCache::Clear() noexcept {
    artifacts_.clear();
}

std::size_t RenderMaterialGraphCompileArtifactCache::Size() const noexcept {
    return artifacts_.size();
}

RenderMaterialGraphCompileArtifactCacheKey BuildRenderMaterialGraphCompileArtifactCacheKey(
    const RenderMaterialGraphDocument& graph,
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies,
    std::uint64_t shaderIncludeHash) {
    std::ostringstream canonicalGraph;
    WriteRenderMaterialGraphDocument(canonicalGraph, graph);

    std::uint64_t graphHash = 1469598103934665603ULL;
    HashString64(graphHash, canonicalGraph.str());

    std::vector<RenderMaterialGraphDependencyHashInput> sortedDependencies(dependencies.begin(), dependencies.end());
    std::sort(sortedDependencies.begin(), sortedDependencies.end(), [](const RenderMaterialGraphDependencyHashInput& lhs, const RenderMaterialGraphDependencyHashInput& rhs) {
        if (lhs.assetId != rhs.assetId) {
            return lhs.assetId < rhs.assetId;
        }
        return lhs.name < rhs.name;
    });

    std::uint64_t dependencyHash = 1469598103934665603ULL;
    for (const RenderMaterialGraphDependencyHashInput& dependency : sortedDependencies) {
        HashString64(dependencyHash, dependency.name);
        HashString64(dependencyHash, std::to_string(dependency.assetId));
        HashString64(dependencyHash, std::to_string(dependency.contentHash));
    }

    std::uint64_t combined = 1469598103934665603ULL;
    HashString64(combined, std::to_string(graphHash));
    HashString64(combined, std::to_string(dependencyHash));
    HashString64(combined, std::to_string(shaderIncludeHash));
    return RenderMaterialGraphCompileArtifactCacheKey{
        .graphContentHash = graphHash,
        .dependencyHash = dependencyHash,
        .shaderIncludeHash = shaderIncludeHash,
        .combinedHash = combined,
    };
}

RenderMaterialGraphCompileArtifactCacheResult CompileRenderMaterialGraphWithArtifactCache(
    RenderMaterialGraphCompileArtifactCache& cache,
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context,
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies,
    std::uint64_t shaderIncludeHash) {
    RenderMaterialGraphCompileArtifactCacheResult result{};
    result.key = BuildRenderMaterialGraphCompileArtifactCacheKey(graph, dependencies, shaderIncludeHash);
    if (const RenderMaterialGraphCompileArtifact* cached = cache.Find(result.key)) {
        result.compile.shader = cached->shader;
        result.compile.diagnostics = ValidateRenderMaterialGraphDocument(graph);
        AttachDiagnosticContext(graph, context, result.compile.diagnostics);
        result.cacheHit = true;
        return result;
    }

    result.compile = CompileRenderMaterialGraphToShaderSource(graph, context);
    if (result.compile.Succeeded()) {
        cache.Store(RenderMaterialGraphCompileArtifact{
            .key = result.key,
            .shader = result.compile.shader,
        });
    }
    return result;
}

RenderMaterialGraphIrBuildResult BuildRenderMaterialGraphIr(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphIrBuildResult result{};

    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (!IsKnownNodeKind(node.kind)) {
            continue;
        }
        RenderMaterialGraphIrNode irNode{
            .nodeId = node.id,
            .kind = node.kind,
        };
        AppendIrPins(irNode);
        result.ir.nodes.push_back(std::move(irNode));
    }

    for (const RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr || !IsKnownNodeKind(fromNode->kind) || !IsKnownNodeKind(toNode->kind)) {
            continue;
        }

        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, link.fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(toNode->kind, link.toPin, false);
        const std::uint32_t fromPinId = link.fromPinId != 0U
            ? link.fromPinId
            : RenderMaterialGraphStablePinId(fromNode->kind, link.fromPin, true);
        const std::uint32_t toPinId = link.toPinId != 0U
            ? link.toPinId
            : RenderMaterialGraphStablePinId(toNode->kind, link.toPin, false);
        const std::uint32_t linkId = link.id != 0U
            ? link.id
            : MakeRenderMaterialGraphLinkId(RenderMaterialGraphLink{
                .fromNodeId = link.fromNodeId,
                .fromPinId = fromPinId,
                .toNodeId = link.toNodeId,
                .toPinId = toPinId,
            });

        result.ir.links.push_back(RenderMaterialGraphIrLink{
            .linkId = linkId,
            .fromNodeId = link.fromNodeId,
            .fromPinId = fromPinId,
            .fromPin = link.fromPin,
            .fromType = fromType,
            .toNodeId = link.toNodeId,
            .toPinId = toPinId,
            .toPin = link.toPin,
            .toType = toType,
        });

        if (toNode->kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            result.ir.outputBindings.push_back(RenderMaterialGraphIrOutputBinding{
                .outputPin = link.toPin,
                .outputNodeId = link.toNodeId,
                .outputPinId = toPinId,
                .outputType = toType,
                .sourceNodeId = link.fromNodeId,
                .sourcePinId = fromPinId,
                .sourcePin = link.fromPin,
                .sourceType = fromType,
            });
        }
    }

    result.diagnostics = ValidateRenderMaterialGraphDocument(graph);
    AttachDiagnosticContext(graph, context, result.diagnostics);
    return result;
}

RenderMaterialGraphCompileResult CompileRenderMaterialGraphToShaderSource(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context) {
    g_renderMaterialGraphCompileInvocationCount.fetch_add(1U, std::memory_order_relaxed);
    RenderMaterialGraphCompileResult result{};
    RenderMaterialGraphIrBuildResult ir = BuildRenderMaterialGraphIr(graph, context);
    result.diagnostics = std::move(ir.diagnostics);
    if (!ir.Succeeded()) {
        return result;
    }

    // MAT-34: only the Surface domain is implemented. A graph requesting another domain falls back to a
    // Surface shader with a warning so it still renders (no false claim of e.g. post-process/decal output).
    const RenderMaterialDomain requestedDomain = ParseRenderMaterialDomain(graph.materialDomain);
    if (!IsRenderMaterialDomainProduction(requestedDomain)) {
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
            .kind = RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain,
            .message = "Material domain '" + std::string{ RenderMaterialDomainName(requestedDomain) } +
                "' is declared but not implemented; compiling as the Surface domain (fallback).",
        });
    }

    // MAT-37: resolve the surface shading model. Unlit and DefaultLit are implemented; any other model
    // falls back to DefaultLit with a diagnostic so a graph never silently shades with an unimplemented model.
    const RenderMaterialShadingModel requestedShadingModel = ParseRenderMaterialShadingModel(graph.shadingModel);
    RenderMaterialShadingModel resolvedShadingModel = requestedShadingModel;
    if (!IsRenderMaterialShadingModelProduction(requestedShadingModel)) {
        resolvedShadingModel = RenderMaterialShadingModel::DefaultLit;
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
            .kind = RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel,
            .message = "Shading model '" + std::string{ RenderMaterialShadingModelName(requestedShadingModel) } +
                "' is declared but not implemented; shading as DefaultLit (fallback).",
        });
    }

    // MAT-38: resolve the blend mode. All seven modes are implemented, so there is no fallback; the value
    // selects the masked clip in the wrapper and the transparent cook/scene blend equation downstream.
    const RenderMaterialGraphBlendMode resolvedBlendMode = ParseRenderMaterialGraphBlendMode(graph.blendMode);

    // MAT-39: each StaticSwitch doubles the potential shader-permutation count (2^n). Warn past a budget so
    // authors see the combinatorial cost before it explodes the cook.
    constexpr std::size_t kStaticSwitchPermutationWarnThreshold = 8U; // 2^8 = 256 permutations.
    std::size_t staticSwitchCount = 0U;
    for (const RenderMaterialGraphNode& staticNode : graph.nodes) {
        if (staticNode.kind == RenderMaterialGraphNodeKind::StaticSwitch) {
            ++staticSwitchCount;
        }
    }
    if (staticSwitchCount > kStaticSwitchPermutationWarnThreshold) {
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Warning,
            .kind = RenderMaterialGraphDiagnosticKind::StaticPermutationExplosion,
            .message = "Graph has " + std::to_string(staticSwitchCount) +
                " static switches (up to 2^" + std::to_string(staticSwitchCount) +
                " shader permutations); consider reducing static branching.",
        });
    }

    const RenderMaterialGraphNode* outputNode = nullptr;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            outputNode = &node;
            break;
        }
    }
    if (outputNode == nullptr) {
        return result;
    }

    std::vector<std::uint32_t> reachable;
    reachable.push_back(outputNode->id);
    for (std::size_t i = 0U; i < reachable.size(); ++i) {
        const std::uint32_t currentId = reachable[i];
        for (const RenderMaterialGraphLink& link : graph.links) {
            if (link.toNodeId == currentId) {
                if (std::find(reachable.begin(), reachable.end(), link.fromNodeId) == reachable.end()) {
                    reachable.push_back(link.fromNodeId);
                }
            }
        }
    }

    struct ReflectionUniformEntry {
        std::string name;
        std::string stableId;
        RenderMaterialGraphNodeKind kind;
    };
    struct ReflectionTextureEntry {
        std::string samplerName;
        std::string stableId;
        RenderMaterialTextureColorSpace colorSpace;
        RenderMaterialGraphSamplerState samplerState;
    };

    std::vector<ReflectionUniformEntry> uniformEntries;
    std::vector<ReflectionTextureEntry> textureEntries;
    bool needsUv0 = false;

    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (std::find(reachable.begin(), reachable.end(), node.id) == reachable.end()) {
            continue;
        }
        switch (node.kind) {
        case RenderMaterialGraphNodeKind::ParameterScalar:
            uniformEntries.push_back({ ParameterUniformName(node, ""), StableParameterId(node), node.kind });
            break;
        case RenderMaterialGraphNodeKind::ParameterVector:
            uniformEntries.push_back({ ParameterUniformName(node, "_xyz"), StableParameterId(node), node.kind });
            break;
        case RenderMaterialGraphNodeKind::ParameterColor:
            uniformEntries.push_back({ ParameterUniformName(node, "_rgba"), StableParameterId(node), node.kind });
            break;
        case RenderMaterialGraphNodeKind::TextureSample:
            if (!HasInputLink(graph, node.id, "texture")) {
                const std::string textureRole = EffectiveTextureRoleForNode(node);
                textureEntries.push_back({ ParameterUniformName(node, "_texture"), StableParameterId(node), EffectiveTextureColorSpaceForNode(node, textureRole), node.parameter.samplerState });
            }
            needsUv0 = true;
            break;
        case RenderMaterialGraphNodeKind::Uv:
        case RenderMaterialGraphNodeKind::TextureCoordinate:
        case RenderMaterialGraphNodeKind::Panner:
        case RenderMaterialGraphNodeKind::Rotator:
        case RenderMaterialGraphNodeKind::BumpOffset:
            // MAT-45: these coordinate nodes sample the uv0 set (directly or as the default coordinate).
            needsUv0 = true;
            break;
        default:
            break;
        }
    }

    std::sort(uniformEntries.begin(), uniformEntries.end(), [](const ReflectionUniformEntry& a, const ReflectionUniformEntry& b) {
        return a.stableId < b.stableId;
    });
    std::sort(textureEntries.begin(), textureEntries.end(), [](const ReflectionTextureEntry& a, const ReflectionTextureEntry& b) {
        return a.stableId < b.stableId;
    });

    // MAT-78: graph textures occupy stages [base, base+count). Reject graphs whose sampler count would
    // overflow the conservative per-backend ceiling instead of emitting a shader that fails to bind.
    const std::uint32_t availableGraphSamplers = kRenderMaterialGraphMaxTextureSamplers - kRenderMaterialGraphTextureBaseSlot;
    if (textureEntries.size() > availableGraphSamplers) {
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Error,
            .kind = RenderMaterialGraphDiagnosticKind::TextureSamplerLimitExceeded,
            .message = "Material graph declares " + std::to_string(textureEntries.size()) +
                " texture samplers but only " + std::to_string(availableGraphSamplers) +
                " are available (stages " + std::to_string(kRenderMaterialGraphTextureBaseSlot) + ".." +
                std::to_string(kRenderMaterialGraphMaxTextureSamplers - 1U) + "); reduce TextureSample nodes or share textures.",
        });
        AttachDiagnosticContext(graph, context, result.diagnostics);
        return result;
    }

    std::string source;
    for (const ReflectionUniformEntry& u : uniformEntries) {
        source += "uniform vec4 " + u.name + ";\n";
    }
    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(textureEntries.size()); ++index) {
        const std::uint32_t stage = kRenderMaterialGraphTextureBaseSlot + index;
        source += "SAMPLER2D(" + textureEntries[index].samplerName + ", " + std::to_string(stage) + ");\n";
    }
    if (!uniformEntries.empty() || !textureEntries.empty()) {
        source += "\n";
    }

    source += "struct MaterialGraphContext {\n";
    source += "    vec2 uv0;\n";
    source += "    vec2 uv1;\n";
    source += "    vec3 normal;\n";
    source += "    vec3 tangent;\n";
    source += "    vec3 bitangent;\n";
    source += "    vec3 worldPos;\n";
    source += "    vec3 viewDir;\n";
    source += "    vec4 vertexColor;\n";
    source += "    float time;\n";
    source += "    vec2 screenPosition;\n";
    source += "    vec3 localPosition;\n";
    source += "    vec3 objectPosition;\n";
    source += "    float perInstanceRandom;\n";
    source += "    float objectRadius;\n";
    // MAT-46: world/object-space inputs populated by the wrapper in every pass so the nodes are shadow-safe.
    source += "    vec3 cameraPosition;\n";
    source += "    vec3 lightVector;\n";
    source += "    vec2 viewSize;\n";
    source += "};\n\n";
    source += "struct MaterialSurface {\n";
    source += "    vec4 baseColor;\n";
    source += "    float metallic;\n";
    source += "    float roughness;\n";
    source += "    vec3 normal;\n";
    source += "    float occlusion;\n";
    source += "    vec3 emissive;\n";
    source += "    float alpha;\n";
    source += "    float alphaClipThreshold;\n";
    source += "    float specular;\n";
    source += "    float anisotropy;\n";
    source += "    vec3 tangent;\n";
    source += "    vec3 subsurfaceColor;\n";
    source += "    float clearCoat;\n";
    source += "    float clearCoatRoughness;\n";
    source += "    float refraction;\n";
    source += "    float surfaceThickness;\n";
    source += "};\n\n";

    GraphCodegen cg{ .graph = graph, .diagnostics = result.diagnostics };
    for (const RenderMaterialGraphLink& link : graph.links) {
        ++cg.fanOut[link.fromNodeId];
    }
    const auto compileOutput = [&cg, outputNode](std::string_view outputPin, RenderMaterialGraphPinType outputType, std::string fallback) {
        return CompileInputExpression(cg, *outputNode, outputPin, outputType, std::move(fallback));
    };

    source += "MaterialSurface EvaluateMaterialGraph(MaterialGraphContext ctx) {\n";
    source += "    MaterialSurface material;\n";

    // MAT-36: when a single MaterialAttributes set is wired into MaterialOutput.attributes it drives the
    // whole surface (UE's "Use Material Attributes" mode); the per-channel pins are bypassed entirely.
    if (HasInputLink(graph, outputNode->id, "attributes")) {
        const std::string attributesExpr = compileOutput("attributes", RenderMaterialGraphPinType::MaterialAttributes, "");
        source += cg.statements;
        source += "    material = " + attributesExpr + ";\n";
    } else {
        const std::string baseColorExpr = compileOutput("baseColor", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)");
        const std::string metallicExpr = compileOutput("metallic", RenderMaterialGraphPinType::Float, "0.0");
        const std::string roughnessExpr = compileOutput("roughness", RenderMaterialGraphPinType::Float, "1.0");
        const std::string normalExpr = compileOutput("normal", RenderMaterialGraphPinType::Normal, "vec3(0.0, 0.0, 1.0)");
        const std::string occlusionExpr = compileOutput("occlusion", RenderMaterialGraphPinType::Float, "1.0");
        const std::string emissiveExpr = compileOutput("emissive", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)");
        const std::string alphaExpr = compileOutput("alpha", RenderMaterialGraphPinType::Float, "material.baseColor.a");
        const std::string alphaClipThresholdExpr = compileOutput("alphaClipThreshold", RenderMaterialGraphPinType::Float, "0.5");
        // MAT-35: advanced fragment-domain surface outputs. They extend the surface contract; advanced shading
        // models (#24) consume them, the base forward lighting uses the core PBR subset.
        const std::string specularExpr = compileOutput("specular", RenderMaterialGraphPinType::Float, "0.5");
        const std::string anisotropyExpr = compileOutput("anisotropy", RenderMaterialGraphPinType::Float, "0.0");
        const std::string tangentExpr = compileOutput("tangent", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)");
        const std::string subsurfaceColorExpr = compileOutput("subsurfaceColor", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)");
        const std::string clearCoatExpr = compileOutput("clearCoat", RenderMaterialGraphPinType::Float, "0.0");
        const std::string clearCoatRoughnessExpr = compileOutput("clearCoatRoughness", RenderMaterialGraphPinType::Float, "0.0");
        const std::string refractionExpr = compileOutput("refraction", RenderMaterialGraphPinType::Float, "0.0");
        const std::string surfaceThicknessExpr = compileOutput("surfaceThickness", RenderMaterialGraphPinType::Float, "0.0");

        source += cg.statements;
        source += "    material.baseColor = " + baseColorExpr + ";\n";
        source += "    material.metallic = " + metallicExpr + ";\n";
        source += "    material.roughness = " + roughnessExpr + ";\n";
        source += "    material.normal = " + normalExpr + ";\n";
        source += "    material.occlusion = " + occlusionExpr + ";\n";
        source += "    material.emissive = " + emissiveExpr + ".rgb;\n";
        source += "    material.alpha = " + alphaExpr + ";\n";
        source += "    material.alphaClipThreshold = " + alphaClipThresholdExpr + ";\n";
        source += "    material.specular = " + specularExpr + ";\n";
        source += "    material.anisotropy = " + anisotropyExpr + ";\n";
        source += "    material.tangent = " + tangentExpr + ";\n";
        source += "    material.subsurfaceColor = " + subsurfaceColorExpr + ".rgb;\n";
        source += "    material.clearCoat = " + clearCoatExpr + ";\n";
        source += "    material.clearCoatRoughness = " + clearCoatRoughnessExpr + ";\n";
        source += "    material.refraction = " + refractionExpr + ";\n";
        source += "    material.surfaceThickness = " + surfaceThicknessExpr + ";\n";
    }
    source += "    return material;\n";
    source += "}\n";

    // MAT-81: world position offset is a vertex-domain output. Compile it with a fresh codegen so the
    // generated function only contains the WPO subgraph, then the generated vertex shader evaluates it
    // with a vertex-populated context and offsets the world position before projection.
    const bool hasWorldPositionOffset = HasInputLink(graph, outputNode->id, "worldPositionOffset");
    if (hasWorldPositionOffset) {
        GraphCodegen wpoCg{ .graph = graph, .diagnostics = result.diagnostics };
        for (const RenderMaterialGraphLink& link : graph.links) {
            ++wpoCg.fanOut[link.fromNodeId];
        }
        const std::string wpoExpr = CompileInputExpression(wpoCg, *outputNode, "worldPositionOffset", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)");
        source += "\nvec3 EvaluateWorldPositionOffset(MaterialGraphContext ctx) {\n";
        source += wpoCg.statements;
        source += "    return " + wpoExpr + ";\n";
        source += "}\n";
    }

    AttachDiagnosticContext(graph, context, result.diagnostics);
    if (!result.Succeeded()) {
        return result;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    HashString64(hash, source);

    RenderMaterialGraphReflection reflection;
    for (const ReflectionUniformEntry& u : uniformEntries) {
        reflection.uniforms.push_back(RenderMaterialGraphReflectionUniform{
            .name = u.name,
            .stableId = u.stableId,
            .kind = u.kind,
        });
    }
    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(textureEntries.size()); ++index) {
        reflection.textures.push_back(RenderMaterialGraphReflectionTexture{
            .samplerName = textureEntries[index].samplerName,
            .stableId = textureEntries[index].stableId,
            .slot = kRenderMaterialGraphTextureBaseSlot + index,
            .colorSpace = textureEntries[index].colorSpace,
            .samplerState = textureEntries[index].samplerState,
        });
    }
    if (needsUv0) {
        reflection.requiredVaryings.push_back("uv0");
    }
    reflection.hasWorldPositionOffset = hasWorldPositionOffset;
    reflection.shadingModel = resolvedShadingModel;
    reflection.blendMode = resolvedBlendMode;

    result.shader = RenderMaterialGraphShaderSource{
        .entryPoint = "EvaluateMaterialGraph",
        .source = std::move(source),
        .sourceHash = hash,
        .reflection = std::move(reflection),
    };
    return result;
}

std::uint64_t RenderMaterialGraphCompileInvocationCount() noexcept {
    return g_renderMaterialGraphCompileInvocationCount.load(std::memory_order_relaxed);
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphDocument(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphRenderPath renderPath) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
    const RenderMaterialGraphNode* outputNode = nullptr;
    std::unordered_map<std::string, std::uint32_t> parameterStableIds;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (!IsKnownNodeKind(node.kind)) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::UnsupportedNode,
                node.id,
                0U,
                {},
                "Material graph contains an unsupported node kind.");
            continue;
        }
        const RenderMaterialGraphNodeSupport pathSupport = RenderMaterialGraphNodeSupportForPath(node.kind, renderPath);
        if (pathSupport == RenderMaterialGraphNodeSupport::Unsupported) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode,
                node.id,
                0U,
                {},
                "Material graph node '" + std::string{ RenderMaterialGraphNodeKindName(node.kind) } + "' is unsupported on the " + std::string{ RenderMaterialGraphRenderPathName(renderPath) } + " render path.");
        } else if (pathSupport == RenderMaterialGraphNodeSupport::Experimental || pathSupport == RenderMaterialGraphNodeSupport::FallbackOnly) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Warning,
                RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode,
                node.id,
                0U,
                {},
                "Material graph node '" + std::string{ RenderMaterialGraphNodeKindName(node.kind) } + "' is " + std::string{ RenderMaterialGraphNodeSupportName(pathSupport) } + " on the " + std::string{ RenderMaterialGraphRenderPathName(renderPath) } + " render path.");
        }
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput && outputNode == nullptr) {
            outputNode = &node;
        }
        const bool textureSampleLocalSlot = node.kind == RenderMaterialGraphNodeKind::TextureSample && !HasInputLink(graph, node.id, "texture");
        if (IsRenderMaterialGraphParameterNode(node.kind) || textureSampleLocalSlot) {
            const std::string stableId = StableParameterId(node);
            const auto [it, inserted] = parameterStableIds.emplace(stableId, node.id);
            if (!inserted) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId,
                    node.id,
                    0U,
                    {},
                    "Material graph parameter stable id '" + stableId + "' is already used by node " + std::to_string(it->second) + ".");
            }
        }
        if (node.kind == RenderMaterialGraphNodeKind::ParameterTexture || textureSampleLocalSlot) {
            const std::string textureRole = EffectiveTextureRoleForNode(node);
            const RenderMaterialTextureColorSpace textureColorSpace = EffectiveTextureColorSpaceForNode(node, textureRole);
            const std::optional<RenderMaterialTextureColorSpace> expectedForRole = ExpectedColorSpaceForTextureRole(textureRole);
            if (!expectedForRole.has_value()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole,
                    node.id,
                    0U,
                    {},
                    textureRole.empty()
                        ? "Texture parameter requires an explicit texture role."
                        : "Texture parameter role '" + textureRole + "' is not supported by the graph material schema.");
            } else if (textureColorSpace == RenderMaterialTextureColorSpace::Unknown) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Warning,
                    RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole,
                    node.id,
                    0U,
                    {},
                    "Texture parameter role '" + textureRole + "' should declare an explicit expected color-space.");
            } else if (textureColorSpace != *expectedForRole) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole,
                    node.id,
                    0U,
                    {},
                    "Texture parameter role '" + textureRole + "' expects " + std::string{ TextureColorSpaceName(*expectedForRole) } + " color-space.");
            }
        }
    }

    if (outputNode == nullptr) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput,
            0U,
            0U,
            "baseColor",
            "Material graph requires a Material Output node with a connected BaseColor input.");
    }

    for (const RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::UnsupportedNode,
                fromNode == nullptr ? link.fromNodeId : link.toNodeId,
                link.id,
                {},
                "Material graph link references a missing node.");
            continue;
        }
        if (!IsRenderMaterialGraphOutputPin(fromNode->kind, link.fromPin) ||
            !IsRenderMaterialGraphInputPin(toNode->kind, link.toPin) ||
            !AreRenderMaterialGraphPinsCompatible(fromNode->kind, link.fromPin, toNode->kind, link.toPin)) {
            const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, link.fromPin, true);
            const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(toNode->kind, link.toPin, false);
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::TypeMismatch,
                toNode->id,
                link.id,
                link.toPin,
                "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".");
        }
    }

    if (GraphHasCycle(graph)) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::Cycle,
            0U,
            0U,
            {},
            "Material graph contains a cycle; shader graph evaluation must be acyclic.");
    }

    return diagnostics;
}

const RenderMaterialGraphNode* FindRenderMaterialGraphNode(const RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const RenderMaterialGraphLink* FindRenderMaterialGraphLink(const RenderMaterialGraphDocument& graph, std::uint32_t linkId) noexcept {
    if (linkId == 0U) {
        return nullptr;
    }
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.id == linkId) {
            return &link;
        }
    }
    return nullptr;
}

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
            pin == "anisotropy" ||
            pin == "tangent" ||
            pin == "subsurfaceColor" ||
            pin == "clearCoat" ||
            pin == "clearCoatRoughness" ||
            pin == "refraction" ||
            pin == "surfaceThickness" ||
            pin == "attributes";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" || pin == "alpha";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return pin == "a" || pin == "b" || pin == "factor";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return pin == "attributes" || pin == "baseColor" || pin == "metallic" || pin == "roughness" ||
            pin == "normal" || pin == "emissive" || pin == "occlusion" || pin == "alpha";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        return pin == "value" || pin == "true" || pin == "false";
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return pin == "input";
    case RenderMaterialGraphNodeKind::Panner:
    case RenderMaterialGraphNodeKind::Rotator:
        return pin == "coordinate" || pin == "time";
    case RenderMaterialGraphNodeKind::BumpOffset:
        return pin == "coordinate" || pin == "height";
    case RenderMaterialGraphNodeKind::ConstantBiasScale:
        return pin == "input";
    case RenderMaterialGraphNodeKind::RotateAboutAxis:
        return pin == "axis" || pin == "angle" || pin == "position";
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "texture" || pin == "uv";
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
        return pin == "a" || pin == "b";
    case RenderMaterialGraphNodeKind::Power:
        return pin == "base" || pin == "exponent";
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
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
        return pin == "value";
    case RenderMaterialGraphNodeKind::ArcTangent2:
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
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return false;
    }
    return false;
}

bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
        return pin == "value";
    case RenderMaterialGraphNodeKind::Desaturate:
        return pin == "color";
    case RenderMaterialGraphNodeKind::BreakVector:
        return pin == "x" || pin == "y" || pin == "z" || pin == "w";
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return pin == "xy";
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        return pin == "xyz";
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return pin == "rgba";
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "color" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::VertexColor:
        return pin == "rgba" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return pin == "xy";
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        return pin == "xyz";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return pin == "texture";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return pin == "normal";
    case RenderMaterialGraphNodeKind::Uv:
        return pin == "uv";
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return pin == "value";
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" || pin == "alpha";
    case RenderMaterialGraphNodeKind::BlendMaterialAttributes:
        return pin == "attributes";
    case RenderMaterialGraphNodeKind::GetMaterialAttributes:
        return pin == "baseColor" || pin == "metallic" || pin == "roughness" || pin == "normal" ||
            pin == "emissive" || pin == "occlusion" || pin == "alpha";
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        return pin == "attributesOut";
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        return pin == "value";
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::StaticComponentMask:
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
        return pin == "value";
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return false;
    }
    return false;
}

std::string_view RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case RenderMaterialGraphPinType::Unknown:
        return "unknown";
    case RenderMaterialGraphPinType::Float:
        return "float";
    case RenderMaterialGraphPinType::Float2:
        return "float2";
    case RenderMaterialGraphPinType::Float3:
        return "float3";
    case RenderMaterialGraphPinType::Float4:
        return "float4";
    case RenderMaterialGraphPinType::Color:
        return "color";
    case RenderMaterialGraphPinType::Texture2D:
        return "texture2D";
    case RenderMaterialGraphPinType::Sampler:
        return "sampler";
    case RenderMaterialGraphPinType::Normal:
        return "normal";
    case RenderMaterialGraphPinType::Bool:
        return "bool";
    }
    return "unknown";
}

RenderMaterialGraphPinType RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind kind, std::string_view pin, bool outputPin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        if (outputPin) return RenderMaterialGraphPinType::Unknown;
        if (pin == "attributes") return RenderMaterialGraphPinType::MaterialAttributes;
        if (pin == "baseColor" || pin == "emissive" || pin == "subsurfaceColor") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "worldPositionOffset" || pin == "tangent") return RenderMaterialGraphPinType::Float3;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha" || pin == "alphaClipThreshold" ||
            pin == "specular" || pin == "anisotropy" || pin == "clearCoat" || pin == "clearCoatRoughness" || pin == "refraction" || pin == "surfaceThickness") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        if (outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::BreakMaterialAttributes:
        if (!outputPin) return pin == "attributes" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha") return RenderMaterialGraphPinType::Float;
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
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::SetMaterialAttributes:
        if (outputPin) return pin == "attributesOut" ? RenderMaterialGraphPinType::MaterialAttributes : RenderMaterialGraphPinType::Unknown;
        if (pin == "attributes") return RenderMaterialGraphPinType::MaterialAttributes;
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha") return RenderMaterialGraphPinType::Float;
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
    case RenderMaterialGraphNodeKind::TextureSample:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::Texture2D;
        if (!outputPin && pin == "uv") return RenderMaterialGraphPinType::Float2;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        return outputPin && pin == "xy" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        return outputPin && pin == "xyz" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return outputPin && pin == "rgba" ? RenderMaterialGraphPinType::Color : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return outputPin && pin == "texture" ? RenderMaterialGraphPinType::Texture2D : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
        if (!outputPin && (pin == "a" || pin == "b")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::Power:
        if (!outputPin && (pin == "base" || pin == "exponent")) return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
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
        if (!outputPin && pin == "value") return RenderMaterialGraphPinType::Float4;
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float4 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ArcTangent2:
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
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::VertexColor:
        if (outputPin && pin == "rgba") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return outputPin && pin == "xy" ? RenderMaterialGraphPinType::Float2 : RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        return outputPin && pin == "xyz" ? RenderMaterialGraphPinType::Float3 : RenderMaterialGraphPinType::Unknown;
    }
    return RenderMaterialGraphPinType::Unknown;
}

bool AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType from, RenderMaterialGraphPinType to) noexcept {
    if (from == RenderMaterialGraphPinType::Unknown || to == RenderMaterialGraphPinType::Unknown) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if ((from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float4) ||
        (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Color)) {
        return true;
    }
    if ((from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float3) ||
        (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Color)) {
        return true;
    }
    if ((from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float3) ||
        (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Float4)) {
        return true;
    }
    if (from == RenderMaterialGraphPinType::Normal && to == RenderMaterialGraphPinType::Float4) {
        return true;
    }
    if ((from == RenderMaterialGraphPinType::Normal && to == RenderMaterialGraphPinType::Float3) ||
        (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Normal)) {
        return true;
    }
    if (from == RenderMaterialGraphPinType::Float && to == RenderMaterialGraphPinType::Float4) {
        return true;
    }
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float) {
        return true;
    }
    return false;
}

bool AreRenderMaterialGraphPinsCompatible(
    RenderMaterialGraphNodeKind fromKind,
    std::string_view fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::string_view toPin) noexcept {
    return AreRenderMaterialGraphPinsCompatible(
        RenderMaterialGraphPinDataType(fromKind, fromPin, true),
        RenderMaterialGraphPinDataType(toKind, toPin, false));
}

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
        if (!outputPin && pin == "anisotropy") return PinId(nodeKind, direction, 11U);
        if (!outputPin && pin == "tangent") return PinId(nodeKind, direction, 12U);
        if (!outputPin && pin == "subsurfaceColor") return PinId(nodeKind, direction, 13U);
        if (!outputPin && pin == "clearCoat") return PinId(nodeKind, direction, 14U);
        if (!outputPin && pin == "clearCoatRoughness") return PinId(nodeKind, direction, 15U);
        if (!outputPin && pin == "refraction") return PinId(nodeKind, direction, 16U);
        if (!outputPin && pin == "surfaceThickness") return PinId(nodeKind, direction, 17U);
        if (!outputPin && pin == "attributes") return PinId(nodeKind, direction, 18U);
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
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Distance:
        if (!outputPin && pin == "a") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "b") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Power:
        if (!outputPin && pin == "base") return PinId(nodeKind, direction, 1U);
        if (!outputPin && pin == "exponent") return PinId(nodeKind, direction, 2U);
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
        if (!outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ArcTangent2:
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
    case RenderMaterialGraphNodeKind::ParameterScalar:
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        if (outputPin && pin == "xy") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::ParameterTexture:
        if (outputPin && pin == "texture") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Uv:
        if (outputPin && pin == "uv") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
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
        if (outputPin && pin == "value") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::VertexColor:
        if (outputPin && pin == "rgba") return PinId(nodeKind, direction, 1U);
        if (outputPin && pin == "r") return PinId(nodeKind, direction, 2U);
        if (outputPin && pin == "g") return PinId(nodeKind, direction, 3U);
        if (outputPin && pin == "b") return PinId(nodeKind, direction, 4U);
        if (outputPin && pin == "a") return PinId(nodeKind, direction, 5U);
        return 0U;
    case RenderMaterialGraphNodeKind::ScreenPosition:
        if (outputPin && pin == "xy") return PinId(nodeKind, direction, 1U);
        return 0U;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        if (outputPin && pin == "xyz") return PinId(nodeKind, direction, 1U);
        return 0U;
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
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
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
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Uv:
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return false;
    }
    return false;
}

RenderMaterialTypeSchema BuildRenderMaterialGraphParameterSchema(
    const RenderMaterialGraphDocument& graph,
    std::string typeName,
    std::uint32_t typeVersion) {
    RenderMaterialTypeSchema schema{
        .typeName = std::move(typeName),
        .typeVersion = typeVersion,
    };

    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (!IsRenderMaterialGraphParameterNode(node.kind) &&
            (node.kind != RenderMaterialGraphNodeKind::TextureSample || HasInputLink(graph, node.id, "texture"))) {
            continue;
        }

        const std::string stableId = StableParameterId(node);
        const std::string displayName = DisplayNameForParameter(node);
        if (node.kind == RenderMaterialGraphNodeKind::ParameterTexture || node.kind == RenderMaterialGraphNodeKind::TextureSample) {
            const std::string textureRole = EffectiveTextureRoleForNode(node);
            schema.textureSlots.push_back(RenderMaterialTextureSlotSchema{
                .name = displayName,
                .role = textureRole,
                .assetIdFieldName = TextureAssetFieldName(stableId),
                .pathFieldName = TexturePathFieldName(stableId),
                .expectedColorSpace = EffectiveTextureColorSpaceForNode(node, textureRole),
                .runtimeSupport = RenderMaterialFeatureSupport::Supported,
                .description = node.parameter.description,
                .fallbackDescription = node.parameter.defaultValueHint,
                .overrideSupported = node.parameter.overrideSupported,
                .editorOrder = node.parameter.editorOrder,
            });
            continue;
        }

        schema.parameters.push_back(RenderMaterialParameterSchema{
            .name = stableId,
            .displayName = displayName,
            .type = ParameterTypeForNode(node.kind),
            .group = node.parameter.group,
            .runtimeSupport = RenderMaterialFeatureSupport::Supported,
            .range = node.parameter.hasRange ? std::optional<RenderMaterialParameterRange>{ RenderMaterialParameterRange{ node.parameter.rangeMin, node.parameter.rangeMax } } : std::nullopt,
            .defaultValueHint = node.parameter.defaultValueHint,
            .description = node.parameter.description,
            .overrideSupported = node.parameter.overrideSupported,
            .editorOrder = node.parameter.editorOrder,
        });
    }

    return schema;
}

RenderMaterialGraphMaterialTypeBuildResult BuildRenderMaterialGraphMaterialTypeDocument(
    const RenderMaterialGraphDocument& graph,
    std::string typeName,
    std::uint32_t typeVersion,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphMaterialTypeBuildResult result{};
    RenderMaterialGraphCompileResult compile = CompileRenderMaterialGraphToShaderSource(graph, context);
    result.diagnostics = std::move(compile.diagnostics);
    if (!compile.Succeeded()) {
        return result;
    }

    RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(graph, typeName, typeVersion);
    schema.alphaModes = { "OPAQUE", "MASK", "BLEND" };
    schema.unsupportedAdvancedFeatures = { "transparentRuntimePass" };

    const std::string graphFragmentShader = "graph_fs_" + std::to_string(compile.shader.sourceHash);
    std::vector<RenderMaterialTypeRequiredResource> requiredResources{
        { .name = "vs_mesh_instanced", .kind = "vertexShader", .required = true },
        { .name = graphFragmentShader, .kind = "fragmentShaderSource", .required = true },
        { .name = "vs_mesh_shadow_instanced", .kind = "vertexShader", .required = true },
        { .name = "fs_mesh_shadow_instanced", .kind = "fragmentShader", .required = true },
    };
    for (const RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        requiredResources.push_back(RenderMaterialTypeRequiredResource{
            .name = slot.assetIdFieldName,
            .kind = "texture",
            .required = false,
        });
    }

    result.document = RenderMaterialTypeDocument{
        .documentVersion = kRenderMaterialTypeDocumentVersion,
        .stableTypeId = typeName,
        .version = typeVersion,
        .displayName = typeName,
        .description = "Material Type generated from a material graph.",
        .domain = RenderMaterialDomain::Surface,
        .shaderModel = RenderMaterialShaderModel::MetallicRoughnessPbr,
        .defaultBlendMode = RenderMaterialBlendMode::Opaque,
        .defaultCullMode = RenderMaterialCullMode::BackFace,
        .renderPasses = std::vector<RenderMaterialTypeRenderPass>{
            RenderMaterialTypeRenderPass{ .name = "BaseOpaque", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = graphFragmentShader },
            RenderMaterialTypeRenderPass{ .name = "ShadowDepth", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_shadow_instanced", .fragmentShader = "fs_mesh_shadow_instanced" },
            RenderMaterialTypeRenderPass{ .name = "BaseTransparent", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = graphFragmentShader },
        },
        .permutationKeys = std::vector<RenderMaterialTypePermutationKey>{
            RenderMaterialTypePermutationKey{ .name = "alphaMode", .defaultValue = "OPAQUE", .allowedValues = std::vector<std::string>{ "OPAQUE", "MASK", "BLEND" } },
            RenderMaterialTypePermutationKey{ .name = "doubleSided", .defaultValue = "false", .allowedValues = std::vector<std::string>{ "false", "true" } },
            RenderMaterialTypePermutationKey{ .name = "graphSourceHash", .defaultValue = std::to_string(compile.shader.sourceHash), .allowedValues = std::vector<std::string>{ std::to_string(compile.shader.sourceHash) } },
        },
        .requiredResources = std::move(requiredResources),
        .schema = std::move(schema),
    };
    return result;
}

RenderMaterialGraphArtifactDecision ResolveRenderMaterialGraphArtifactDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state) noexcept {
    if (state.compileState == RenderMaterialGraphArtifactCompileState::Ready && state.currentArtifactAssetId != 0U) {
        return RenderMaterialGraphArtifactDecision{
            .kind = RenderMaterialGraphArtifactDecisionKind::UseCurrentArtifact,
            .artifactAssetId = state.currentArtifactAssetId,
        };
    }

    if (graph.artifactFailurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial &&
        graph.lastGoodArtifact.IsValid()) {
        return RenderMaterialGraphArtifactDecision{
            .kind = RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact,
            .artifactAssetId = graph.lastGoodArtifact.assetId,
        };
    }

    return RenderMaterialGraphArtifactDecision{
        .kind = RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial,
        .artifactAssetId = 0U,
    };
}

bool RenderMaterialGraphArtifactRuntimeDecision::UsesFallback() const noexcept {
    return decision.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact ||
        decision.kind == RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial;
}

RenderMaterialGraphArtifactRuntimeDecision ResolveRenderMaterialGraphArtifactRuntimeDecision(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphArtifactState& state,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphArtifactRuntimeDecision runtime{};
    runtime.decision = ResolveRenderMaterialGraphArtifactDecision(graph, state);
    if (runtime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseCurrentArtifact) {
        return runtime;
    }

    const bool lastGood = runtime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact;
    runtime.diagnostic = RenderMaterialGraphDiagnostic{
        .severity = lastGood ? RenderMaterialGraphDiagnosticSeverity::Warning : RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .assetId = context.assetId,
        .sourcePath = context.sourcePath,
        .message = lastGood
            ? "Material graph compile failed or is pending; using the last-good shader artifact."
            : "Material graph compile failed or is pending and no last-good artifact is available; using the explicit error material.",
    };
    return runtime;
}

RenderMaterialGraphRuntimeState ResolveRenderMaterialGraphRuntimeState(const RenderMaterialGraphRuntimeStateInput& input) noexcept {
    switch (input.phase) {
    case RenderMaterialGraphCompilePhase::Editing:
        return RenderMaterialGraphRuntimeState::Dirty;
    case RenderMaterialGraphCompilePhase::Validating:
        return RenderMaterialGraphRuntimeState::Validating;
    case RenderMaterialGraphCompilePhase::Compiling:
        return RenderMaterialGraphRuntimeState::Compiling;
    case RenderMaterialGraphCompilePhase::Compiled:
        break;
    }

    const bool succeeded = input.validationSucceeded && input.compileSucceeded && input.hasGpuProgram;
    if (succeeded) {
        return RenderMaterialGraphRuntimeState::UsingGpuGraph;
    }
    if (!input.fallbackApplied) {
        return RenderMaterialGraphRuntimeState::CompileFailed;
    }
    if (input.failurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial && input.hasLastGood) {
        return RenderMaterialGraphRuntimeState::UsingLastGood;
    }
    return RenderMaterialGraphRuntimeState::UsingErrorMaterial;
}

std::string_view RenderMaterialGraphRuntimeStateName(RenderMaterialGraphRuntimeState state) noexcept {
    switch (state) {
    case RenderMaterialGraphRuntimeState::Dirty:
        return "Dirty";
    case RenderMaterialGraphRuntimeState::Validating:
        return "Validating";
    case RenderMaterialGraphRuntimeState::Compiling:
        return "Compiling";
    case RenderMaterialGraphRuntimeState::CompileFailed:
        return "CompileFailed";
    case RenderMaterialGraphRuntimeState::UsingLastGood:
        return "UsingLastGood";
    case RenderMaterialGraphRuntimeState::UsingErrorMaterial:
        return "UsingErrorMaterial";
    case RenderMaterialGraphRuntimeState::UsingGpuGraph:
        return "UsingGpuGraph";
    }
    return "Dirty";
}

bool RenderMaterialGraphRuntimeStateUsesFallback(RenderMaterialGraphRuntimeState state) noexcept {
    return state == RenderMaterialGraphRuntimeState::UsingLastGood ||
        state == RenderMaterialGraphRuntimeState::UsingErrorMaterial ||
        state == RenderMaterialGraphRuntimeState::CompileFailed;
}

bool HasGraphAuthoringData(const RenderMaterialGraphDocument& graph) noexcept {
    if (!graph.links.empty()) {
        return true;
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        const bool isImplicitDefault = node.id == 1U &&
            node.kind == RenderMaterialGraphNodeKind::MaterialOutput &&
            node.positionX == 640 &&
            node.positionY == 240 &&
            node.parameter.stableId.empty() &&
            node.parameter.displayName.empty();
        if (!isImplicitDefault) {
            return true;
        }
    }
    return false;
}

MaterialSurface DefaultMaterialSurface() noexcept {
    return {};
}

MaterialGraphContext DefaultMaterialGraphContext() noexcept {
    return {};
}

} // namespace kb::render
