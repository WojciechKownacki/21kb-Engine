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
        kind == RenderMaterialGraphNodeKind::ConstantVector ||
        kind == RenderMaterialGraphNodeKind::ConstantColor;
}

[[nodiscard]] bool ShouldPersistGraphNodeMetadata(const RenderMaterialGraphNode& node) noexcept {
    return IsRenderMaterialGraphParameterNode(node.kind) ||
        IsRenderMaterialGraphConstantNode(node.kind) ||
        (node.kind == RenderMaterialGraphNodeKind::TextureSample && !node.parameter.stableId.empty());
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
        return "vec4(" + expression + ")";
    }
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
    }
    return expression;
}

[[nodiscard]] std::string CompileNodeOutputExpression(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::string_view outputPin,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    std::vector<std::uint32_t>& stack);

[[nodiscard]] std::string CompileInputExpression(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::string_view inputPin,
    RenderMaterialGraphPinType expectedType,
    std::string fallback,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    std::vector<std::uint32_t>& stack) {
    const RenderMaterialGraphLink* link = FindInputLink(graph, node.id, inputPin);
    if (link == nullptr) {
        return fallback;
    }
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link->fromNodeId);
    if (fromNode == nullptr) {
        return fallback;
    }
    const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, link->fromPin, true);
    return CoerceExpression(
        CompileNodeOutputExpression(graph, *fromNode, link->fromPin, diagnostics, stack),
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

[[nodiscard]] std::string CompileNodeOutputExpression(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::string_view outputPin,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    std::vector<std::uint32_t>& stack) {
    if (ContainsNode(stack, node.id)) {
        AddShaderGenerationDiagnostic(diagnostics, node, outputPin, "Material graph shader generation hit a recursive node dependency.");
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node.kind, outputPin, true));
    }
    stack.push_back(node.id);

    std::string expression;
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        expression = ConstantScalarExpression(node);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector:
        expression = ConstantVectorExpression(node);
        break;
    case RenderMaterialGraphNodeKind::ConstantColor:
        expression = ConstantColorExpression(node);
        break;
    case RenderMaterialGraphNodeKind::ParameterScalar:
        expression = ParameterUniformName(node, "");
        break;
    case RenderMaterialGraphNodeKind::ParameterVector:
        expression = ParameterUniformName(node, "_xyz");
        break;
    case RenderMaterialGraphNodeKind::ParameterColor:
        expression = ParameterUniformName(node, "_rgba");
        break;
    case RenderMaterialGraphNodeKind::Add:
        expression = "(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            " + " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Subtract:
        expression = "(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            " - " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Multiply:
        expression = "(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            " * " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Divide:
        expression = "(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            " / max(abs(" +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            "), vec4(0.0001)))";
        break;
    case RenderMaterialGraphNodeKind::Power:
        expression = "pow(max(" +
            CompileInputExpression(graph, node, "base", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", vec4(0.0)), " +
            CompileInputExpression(graph, node, "exponent", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::OneMinus:
        expression = "(vec4(1.0) - " +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Absolute:
        expression = "abs(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Minimum:
        expression = "min(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Maximum:
        expression = "max(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Saturate:
        expression = "clamp(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", vec4(0.0), vec4(1.0))";
        break;
    case RenderMaterialGraphNodeKind::Floor:
        expression = "floor(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Ceil:
        expression = "ceil(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Fraction:
        expression = "fract(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::SquareRoot:
        expression = "sqrt(max(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", vec4(0.0)))";
        break;
    case RenderMaterialGraphNodeKind::Sine:
        expression = "sin(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Cosine:
        expression = "cos(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::DotProduct:
        expression = "dot(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::CrossProduct:
        expression = "cross(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 1.0, 0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Normalize: {
        const std::string vector = CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)", diagnostics, stack);
        expression = "((length(" + vector + ") > 0.0001) ? normalize(" + vector + ") : vec3(0.0, 0.0, 1.0))";
        break;
    }
    case RenderMaterialGraphNodeKind::Length:
        expression = "length(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Distance:
        expression = "distance(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::BreakVector: {
        const std::string value = CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0, 0.0, 0.0, 1.0)", diagnostics, stack);
        if (outputPin == "x") {
            expression = "(" + value + ").x";
        } else if (outputPin == "y") {
            expression = "(" + value + ").y";
        } else if (outputPin == "z") {
            expression = "(" + value + ").z";
        } else if (outputPin == "w") {
            expression = "(" + value + ").w";
        } else {
            AddShaderGenerationDiagnostic(diagnostics, node, outputPin, "BreakVector output pin is not supported.");
            expression = "0.0";
        }
        break;
    }
    case RenderMaterialGraphNodeKind::MakeVector:
        expression = "vec4(" +
            CompileInputExpression(graph, node, "x", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "y", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "z", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "w", RenderMaterialGraphPinType::Float, "1.0", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Step:
        expression = "step(" +
            CompileInputExpression(graph, node, "edge", RenderMaterialGraphPinType::Float4, "vec4(0.5)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::SmoothStep:
        expression = "smoothstep(" +
            CompileInputExpression(graph, node, "min", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "max", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::If: {
        const std::string lhs = CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack);
        const std::string rhs = CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack);
        const std::string less = CompileInputExpression(graph, node, "less", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack);
        const std::string equal = CompileInputExpression(graph, node, "equal", RenderMaterialGraphPinType::Float4, "vec4(0.5)", diagnostics, stack);
        const std::string greater = CompileInputExpression(graph, node, "greater", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack);
        expression = "((" + lhs + " > " + rhs + ") ? " + greater + " : ((abs(" + lhs + " - " + rhs + ") <= 0.0001) ? " + equal + " : " + less + "))";
        break;
    }
    case RenderMaterialGraphNodeKind::Desaturate: {
        const std::string color = CompileInputExpression(graph, node, "color", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)", diagnostics, stack);
        const std::string fraction = CompileInputExpression(graph, node, "fraction", RenderMaterialGraphPinType::Float, "1.0", diagnostics, stack);
        const std::string luma = "dot((" + color + ").rgb, vec3(0.299, 0.587, 0.114))";
        expression = "mix(" + color + ", vec4(vec3(" + luma + "), (" + color + ").a), clamp(" + fraction + ", 0.0, 1.0))";
        break;
    }
    case RenderMaterialGraphNodeKind::Fresnel: {
        const std::string normal = CompileInputExpression(graph, node, "normal", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)", diagnostics, stack);
        const std::string view = CompileInputExpression(graph, node, "view", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)", diagnostics, stack);
        const std::string exponent = CompileInputExpression(graph, node, "exponent", RenderMaterialGraphPinType::Float, "5.0", diagnostics, stack);
        const std::string base = CompileInputExpression(graph, node, "base", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack);
        const std::string facing = "clamp(dot(normalize(" + normal + "), normalize(" + view + ")), 0.0, 1.0)";
        expression = "mix(pow(1.0 - " + facing + ", max(" + exponent + ", 0.0001)), 1.0, clamp(" + base + ", 0.0, 1.0))";
        break;
    }
    case RenderMaterialGraphNodeKind::Negate:
        expression = "-(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ")";
        break;
    case RenderMaterialGraphNodeKind::Sign:
        expression = "sign(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ")";
        break;
    case RenderMaterialGraphNodeKind::Round:
        expression = "round(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ")";
        break;
    case RenderMaterialGraphNodeKind::Truncate: {
        const std::string value = CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack);
        expression = "sign(" + value + ") * floor(abs(" + value + "))";
        break;
    }
    case RenderMaterialGraphNodeKind::Tangent:
        expression = "tan(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ")";
        break;
    case RenderMaterialGraphNodeKind::ArcSine:
        expression = "asin(clamp(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ", vec4(-1.0), vec4(1.0)))";
        break;
    case RenderMaterialGraphNodeKind::ArcCosine:
        expression = "acos(clamp(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) + ", vec4(-1.0), vec4(1.0)))";
        break;
    case RenderMaterialGraphNodeKind::ArcTangent:
        expression = "atan(" + CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) + ")";
        break;
    case RenderMaterialGraphNodeKind::ArcTangent2:
        expression = "atan(" +
            CompileInputExpression(graph, node, "y", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "x", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Clamp:
        expression = "clamp(" +
            CompileInputExpression(graph, node, "value", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "min", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "max", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::Lerp:
        expression = "mix(" +
            CompileInputExpression(graph, node, "a", RenderMaterialGraphPinType::Float4, "vec4(0.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "b", RenderMaterialGraphPinType::Float4, "vec4(1.0)", diagnostics, stack) +
            ", " +
            CompileInputExpression(graph, node, "t", RenderMaterialGraphPinType::Float, "0.0", diagnostics, stack) +
            ")";
        break;
    case RenderMaterialGraphNodeKind::NormalUnpack:
        expression = "normalize((" +
            CompileInputExpression(graph, node, "color", RenderMaterialGraphPinType::Color, "vec4(0.5, 0.5, 1.0, 1.0)", diagnostics, stack) +
            ").rgb * 2.0 - vec3(1.0, 1.0, 1.0))";
        break;
    case RenderMaterialGraphNodeKind::Uv:
        expression = "v_texcoord0";
        break;
    case RenderMaterialGraphNodeKind::TextureSample: {
        const std::string sampled = "texture2D(" +
            CompileTextureInputExpression(graph, node, diagnostics) +
            ", " +
            CompileInputExpression(graph, node, "uv", RenderMaterialGraphPinType::Float2, "v_texcoord0", diagnostics, stack) +
            ")";
        if (outputPin == "r" || outputPin == "g" || outputPin == "b" || outputPin == "a") {
            expression = "(" + sampled + ")." + std::string{ outputPin };
        } else {
            expression = sampled;
        }
        break;
    }
    case RenderMaterialGraphNodeKind::ParameterTexture:
        AddShaderGenerationDiagnostic(diagnostics, node, outputPin, "Texture parameter cannot be emitted as a numeric shader expression without a TextureSample node.");
        expression = DefaultExpressionForType(RenderMaterialGraphPinDataType(node.kind, outputPin, true));
        break;
    case RenderMaterialGraphNodeKind::MaterialOutput:
        expression = DefaultExpressionForType(RenderMaterialGraphPinDataType(node.kind, outputPin, true));
        break;
    }

    stack.pop_back();
    return expression;
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
        break;
    }
    return RenderMaterialParameterType::Scalar;
}

[[nodiscard]] bool IsKnownNodeKind(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
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

std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return "MaterialOutput";
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return "ConstantScalar";
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
    }
    return "unsupported_node";
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

    std::vector<std::uint32_t> stack;
    const auto compileOutput = [&graph, outputNode, &result, &stack](std::string_view outputPin, RenderMaterialGraphPinType outputType, std::string fallback) {
        return CompileInputExpression(graph, *outputNode, outputPin, outputType, std::move(fallback), result.diagnostics, stack);
    };

    std::string source;
    source += "struct GraphMaterial {\n";
    source += "    vec4 baseColor;\n";
    source += "    float metallic;\n";
    source += "    float roughness;\n";
    source += "    vec3 normal;\n";
    source += "    float occlusion;\n";
    source += "    vec3 emissive;\n";
    source += "    float alpha;\n";
    source += "};\n\n";
    source += "GraphMaterial EvaluateMaterialGraph() {\n";
    source += "    GraphMaterial material;\n";
    source += "    material.baseColor = " + compileOutput("baseColor", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)") + ";\n";
    source += "    material.metallic = " + compileOutput("metallic", RenderMaterialGraphPinType::Float, "0.0") + ";\n";
    source += "    material.roughness = " + compileOutput("roughness", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
    source += "    material.normal = " + compileOutput("normal", RenderMaterialGraphPinType::Normal, "vec3(0.0, 0.0, 1.0)") + ";\n";
    source += "    material.occlusion = " + compileOutput("occlusion", RenderMaterialGraphPinType::Float, "1.0") + ";\n";
    source += "    material.emissive = " + compileOutput("emissive", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)") + ".rgb;\n";
    source += "    material.alpha = " + compileOutput("alpha", RenderMaterialGraphPinType::Float, "material.baseColor.a") + ";\n";
    source += "    return material;\n";
    source += "}\n";

    AttachDiagnosticContext(graph, context, result.diagnostics);
    if (!result.Succeeded()) {
        return result;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    HashString64(hash, source);
    result.shader = RenderMaterialGraphShaderSource{
        .entryPoint = "EvaluateMaterialGraph",
        .source = std::move(source),
        .sourceHash = hash,
    };
    return result;
}

std::uint64_t RenderMaterialGraphCompileInvocationCount() noexcept {
    return g_renderMaterialGraphCompileInvocationCount.load(std::memory_order_relaxed);
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphDocument(const RenderMaterialGraphDocument& graph) {
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
            pin == "alpha";
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
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::Uv:
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
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ParameterVector:
        return pin == "xyz";
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return pin == "rgba";
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "color" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return pin == "texture";
    case RenderMaterialGraphNodeKind::NormalUnpack:
        return pin == "normal";
    case RenderMaterialGraphNodeKind::Uv:
        return pin == "uv";
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
        if (pin == "baseColor" || pin == "emissive") return RenderMaterialGraphPinType::Color;
        if (pin == "normal") return RenderMaterialGraphPinType::Normal;
        if (pin == "metallic" || pin == "roughness" || pin == "occlusion" || pin == "alpha") return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::TextureSample:
        if (!outputPin && pin == "texture") return RenderMaterialGraphPinType::Texture2D;
        if (!outputPin && pin == "uv") return RenderMaterialGraphPinType::Float2;
        if (outputPin && pin == "color") return RenderMaterialGraphPinType::Color;
        if (outputPin && (pin == "r" || pin == "g" || pin == "b" || pin == "a")) return RenderMaterialGraphPinType::Float;
        return RenderMaterialGraphPinType::Unknown;
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return outputPin && pin == "value" ? RenderMaterialGraphPinType::Float : RenderMaterialGraphPinType::Unknown;
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
            RenderMaterialTypeRenderPass{ .name = "BaseTransparent", .support = RenderMaterialFeatureSupport::ParsedButIgnored, .vertexShader = "vs_mesh_instanced", .fragmentShader = graphFragmentShader },
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

} // namespace kb::render
