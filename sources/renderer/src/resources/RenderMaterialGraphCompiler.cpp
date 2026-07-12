#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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
        case '\n': encoded += "%0A"; break;
        case '\r': encoded += "%0D"; break;
        case '#': encoded += "%23"; break;
        default: encoded += ch; break;
        }
    }
    return encoded;
}

[[nodiscard]] std::string EncodeCustomPinSpec(std::span<const RenderMaterialGraphCustomPin> pins) {
    if (pins.empty()) {
        return "_";
    }
    std::string spec;
    for (std::size_t index = 0U; index < pins.size(); ++index) {
        if (index != 0U) {
            spec += ",";
        }
        spec += EncodeToken(pins[index].name);
        spec += ":";
        spec += RenderMaterialGraphPinTypeName(pins[index].type);
    }
    return spec;
}

[[nodiscard]] std::string EncodeGraphNodeIdList(std::span<const std::uint32_t> nodeIds) {
    if (nodeIds.empty()) {
        return "_";
    }
    std::string text;
    for (std::size_t index = 0U; index < nodeIds.size(); ++index) {
        if (index != 0U) {
            text += ",";
        }
        text += std::to_string(nodeIds[index]);
    }
    return text;
}

[[nodiscard]] std::vector<float> ParseDefaultNumbers(std::string_view text) {
    std::vector<float> values;
    if (text.empty() || text == "_") {
        return values;
    }
    static_cast<void>(ParseFiniteMaterialFloatSequence(text, values, 1U, 64U));
    return values;
}

[[nodiscard]] std::string FloatLiteral(float value) {
    if (!std::isfinite(value)) {
        return "0.0";
    }
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
        kind == RenderMaterialGraphNodeKind::ConstantColor ||
        kind == RenderMaterialGraphNodeKind::ConstantBool;
}

[[nodiscard]] bool IsRenderMaterialGraphPassThroughNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::Reroute ||
        kind == RenderMaterialGraphNodeKind::CompositeInput ||
        kind == RenderMaterialGraphNodeKind::CompositeOutput ||
        kind == RenderMaterialGraphNodeKind::FunctionInput ||
        kind == RenderMaterialGraphNodeKind::FunctionOutput;
}

[[nodiscard]] bool IsRenderMaterialGraphOrganizationNode(RenderMaterialGraphNodeKind kind) noexcept {
    return IsRenderMaterialGraphPassThroughNode(kind) ||
        kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration ||
        kind == RenderMaterialGraphNodeKind::NamedRerouteUsage ||
        kind == RenderMaterialGraphNodeKind::MaterialFunctionCall ||
        kind == RenderMaterialGraphNodeKind::LayerStack;
}

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

[[nodiscard]] std::string NamedRerouteKey(const RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    if (!node.parameter.displayName.empty()) {
        return node.parameter.displayName;
    }
    return {};
}

[[nodiscard]] bool ShouldPersistGraphNodeMetadata(const RenderMaterialGraphNode& node) noexcept {
    return IsRenderMaterialGraphParameterNode(node.kind) ||
        IsRenderMaterialGraphConstantNode(node.kind) ||
        IsRenderMaterialGraphOrganizationNode(node.kind) ||
        node.kind == RenderMaterialGraphNodeKind::StaticBoolParameter ||
        node.kind == RenderMaterialGraphNodeKind::StaticSwitch ||
        node.kind == RenderMaterialGraphNodeKind::StaticComponentMask ||
        node.kind == RenderMaterialGraphNodeKind::CollectionParameter ||
        node.kind == RenderMaterialGraphNodeKind::ColorRamp ||
        node.kind == RenderMaterialGraphNodeKind::FunctionInput ||
        node.kind == RenderMaterialGraphNodeKind::FunctionOutput ||
        node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall ||
        node.kind == RenderMaterialGraphNodeKind::LayerStack ||
        ((node.kind == RenderMaterialGraphNodeKind::TextureSample ||
          node.kind == RenderMaterialGraphNodeKind::TextureObject ||
          node.kind == RenderMaterialGraphNodeKind::TextureSampleCube ||
          node.kind == RenderMaterialGraphNodeKind::TextureObjectCube ||
          node.kind == RenderMaterialGraphNodeKind::TextureSampleVolume ||
          node.kind == RenderMaterialGraphNodeKind::TextureObjectVolume ||
          node.kind == RenderMaterialGraphNodeKind::TextureSample2DArray ||
          node.kind == RenderMaterialGraphNodeKind::TextureObject2DArray) &&
            !node.parameter.stableId.empty()) ||
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
[[nodiscard]] std::string SanitizeShaderIdentifier(std::string_view text, std::string_view fallback);

[[nodiscard]] bool IsCustomCodeValueType(RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
    case RenderMaterialGraphPinType::Float2:
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
    case RenderMaterialGraphPinType::Normal:
    case RenderMaterialGraphPinType::Bool:
        return true;
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::MaterialAttributes:
        return false;
    }
    return false;
}

[[nodiscard]] bool IsShaderIdentifier(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto isAlpha = [](char ch) noexcept {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    };
    const auto isDigit = [](char ch) noexcept {
        return ch >= '0' && ch <= '9';
    };
    if (!isAlpha(text.front()) && text.front() != '_') {
        return false;
    }
    for (const char ch : text) {
        if (!isAlpha(ch) && !isDigit(ch) && ch != '_') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string CustomCodeOutputTempKey(std::uint32_t nodeId, std::string_view outputPin) {
    return std::to_string(nodeId) + ":" + std::string{ outputPin };
}

[[nodiscard]] std::string CustomCodeFunctionName(const RenderMaterialGraphNode& node) {
    return "kb_custom_" + std::to_string(node.id);
}

[[nodiscard]] std::string CustomCodeTempName(const RenderMaterialGraphNode& node, std::string_view outputPin) {
    return "custom" + std::to_string(node.id) + "_" + SanitizeShaderIdentifier(outputPin, "out");
}

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

void AppendIrPin(RenderMaterialGraphIrNode& irNode, const RenderMaterialGraphNode& node, std::string_view pin, bool outputPin) {
    RenderMaterialGraphIrPin typedPin{
        .name = std::string{ pin },
        .stablePinId = RenderMaterialGraphStablePinId(node, pin, outputPin),
        .type = RenderMaterialGraphPinDataType(node, pin, outputPin),
        .outputPin = outputPin,
    };
    if (outputPin) {
        irNode.outputs.push_back(std::move(typedPin));
    } else {
        irNode.inputs.push_back(std::move(typedPin));
    }
}

void AppendCustomCodePins(RenderMaterialGraphIrNode& irNode, const RenderMaterialGraphNode& node) {
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.inputs) {
        AppendIrPin(irNode, node, pin.name, false);
    }
    AppendIrPin(irNode, node, "value", true);
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.outputs) {
        AppendIrPin(irNode, node, pin.name, true);
    }
}

void AppendMaterialFunctionCallPins(RenderMaterialGraphIrNode& irNode, const RenderMaterialGraphNode& node) {
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.inputs) {
        AppendIrPin(irNode, node, pin.name, false);
    }
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.outputs) {
        AppendIrPin(irNode, node, pin.name, true);
    }
}

void AppendTextureSampleOutputPins(RenderMaterialGraphIrNode& irNode) {
    AppendIrPin(irNode, irNode.kind, "color", true);
    AppendIrPin(irNode, irNode.kind, "r", true);
    AppendIrPin(irNode, irNode.kind, "g", true);
    AppendIrPin(irNode, irNode.kind, "b", true);
    AppendIrPin(irNode, irNode.kind, "a", true);
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
        AppendIrPin(irNode, irNode.kind, "tangentOutput", false);
        AppendIrPin(irNode, irNode.kind, "attributes", false);
        AppendIrPin(irNode, irNode.kind, "customizedUv0", false);
        AppendIrPin(irNode, irNode.kind, "displacement", false);
        break;
    case RenderMaterialGraphNodeKind::MakeMaterialAttributes:
        AppendIrPin(irNode, irNode.kind, "baseColor", false);
        AppendIrPin(irNode, irNode.kind, "metallic", false);
        AppendIrPin(irNode, irNode.kind, "roughness", false);
        AppendIrPin(irNode, irNode.kind, "normal", false);
        AppendIrPin(irNode, irNode.kind, "emissive", false);
        AppendIrPin(irNode, irNode.kind, "occlusion", false);
        AppendIrPin(irNode, irNode.kind, "alpha", false);
        AppendIrPin(irNode, irNode.kind, "alphaClipThreshold", false);
        AppendIrPin(irNode, irNode.kind, "specular", false);
        AppendIrPin(irNode, irNode.kind, "tangentOutput", false);
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
        AppendIrPin(irNode, irNode.kind, "alphaClipThreshold", true);
        AppendIrPin(irNode, irNode.kind, "specular", true);
        AppendIrPin(irNode, irNode.kind, "tangentOutput", true);
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
        AppendIrPin(irNode, irNode.kind, "alphaClipThreshold", true);
        AppendIrPin(irNode, irNode.kind, "specular", true);
        AppendIrPin(irNode, irNode.kind, "tangentOutput", true);
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
        AppendIrPin(irNode, irNode.kind, "alphaClipThreshold", false);
        AppendIrPin(irNode, irNode.kind, "specular", false);
        AppendIrPin(irNode, irNode.kind, "tangentOutput", false);
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
    case RenderMaterialGraphNodeKind::FunctionInput:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::FunctionOutput:
        AppendIrPin(irNode, irNode.kind, "value", false);
        break;
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        break;
    case RenderMaterialGraphNodeKind::LayerStack:
        AppendIrPin(irNode, irNode.kind, "attributes", true);
        break;
    case RenderMaterialGraphNodeKind::QualitySwitch:
        AppendIrPin(irNode, irNode.kind, "low", false);
        AppendIrPin(irNode, irNode.kind, "med", false);
        AppendIrPin(irNode, irNode.kind, "high", false);
        AppendIrPin(irNode, irNode.kind, "epic", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        AppendIrPin(irNode, irNode.kind, "es3", false);
        AppendIrPin(irNode, irNode.kind, "sm5", false);
        AppendIrPin(irNode, irNode.kind, "sm6", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        AppendIrPin(irNode, irNode.kind, "forward", false);
        AppendIrPin(irNode, irNode.kind, "forwardPlus", false);
        AppendIrPin(irNode, irNode.kind, "deferred", false);
        AppendIrPin(irNode, irNode.kind, "result", true);
        break;
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        AppendIrPin(irNode, irNode.kind, "vertex", false);
        AppendIrPin(irNode, irNode.kind, "fragment", false);
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
    case RenderMaterialGraphNodeKind::ObjectOrientation:
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
    case RenderMaterialGraphNodeKind::TwoSidedSign:
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::PixelDepth:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::CameraDepthFade:
        AppendIrPin(irNode, irNode.kind, "fadeLength", false);
        AppendIrPin(irNode, irNode.kind, "fadeOffset", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
        AppendIrPin(irNode, irNode.kind, "uv", false);
        AppendTextureSampleOutputPins(irNode);
        break;
    case RenderMaterialGraphNodeKind::DepthFade:
        AppendIrPin(irNode, irNode.kind, "fadeDistance", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::TextureSample:
        AppendIrPin(irNode, irNode.kind, "texture", false);
        AppendIrPin(irNode, irNode.kind, "uv", false);
        AppendTextureSampleOutputPins(irNode);
        break;
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        AppendIrPin(irNode, irNode.kind, "texture", false);
        AppendIrPin(irNode, irNode.kind, "direction", false);
        AppendTextureSampleOutputPins(irNode);
        break;
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        AppendIrPin(irNode, irNode.kind, "texture", false);
        AppendIrPin(irNode, irNode.kind, "uvw", false);
        AppendTextureSampleOutputPins(irNode);
        break;
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        AppendIrPin(irNode, irNode.kind, "texture", false);
        AppendIrPin(irNode, irNode.kind, "uv", false);
        AppendIrPin(irNode, irNode.kind, "layer", false);
        AppendTextureSampleOutputPins(irNode);
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
    case RenderMaterialGraphNodeKind::Fmod:
    case RenderMaterialGraphNodeKind::SphereMask:
    case RenderMaterialGraphNodeKind::AppendVector:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::InverseLerp:
        AppendIrPin(irNode, irNode.kind, "a", false);
        AppendIrPin(irNode, irNode.kind, "b", false);
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Power:
        AppendIrPin(irNode, irNode.kind, "base", false);
        AppendIrPin(irNode, irNode.kind, "exponent", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::CustomCode:
        AppendIrPin(irNode, irNode.kind, "A", false);
        AppendIrPin(irNode, irNode.kind, "B", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
        AppendIrPin(irNode, irNode.kind, "input", false);
        AppendIrPin(irNode, irNode.kind, "output", true);
        break;
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        AppendIrPin(irNode, irNode.kind, "input", false);
        break;
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        AppendIrPin(irNode, irNode.kind, "output", true);
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
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::Sobol:
        AppendIrPin(irNode, irNode.kind, "cell", false);
        AppendIrPin(irNode, irNode.kind, "index", false);
        AppendIrPin(irNode, irNode.kind, "seed", false);
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
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        AppendIrPin(irNode, irNode.kind, "index", false);
        AppendIrPin(irNode, irNode.kind, "default", false);
        AppendIrPin(irNode, irNode.kind, "case0", false);
        AppendIrPin(irNode, irNode.kind, "case1", false);
        AppendIrPin(irNode, irNode.kind, "case2", false);
        AppendIrPin(irNode, irNode.kind, "case3", false);
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
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
        AppendIrPin(irNode, irNode.kind, "value", false);
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
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
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        AppendIrPin(irNode, irNode.kind, "xy", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector:
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        break;
    case RenderMaterialGraphNodeKind::ParameterVector:
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        break;
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        AppendIrPin(irNode, irNode.kind, "rgba", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        AppendIrPin(irNode, irNode.kind, "a", true);
        break;
    case RenderMaterialGraphNodeKind::CollectionParameter:
        AppendIrPin(irNode, irNode.kind, "value", true);
        AppendIrPin(irNode, irNode.kind, "scalar", true);
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        AppendIrPin(irNode, irNode.kind, "rgba", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        AppendIrPin(irNode, irNode.kind, "a", true);
        break;
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        AppendIrPin(irNode, irNode.kind, "texture", true);
        break;
    case RenderMaterialGraphNodeKind::Uv:
        AppendIrPin(irNode, irNode.kind, "uv", true);
        break;
    case RenderMaterialGraphNodeKind::Time:
    case RenderMaterialGraphNodeKind::DeltaTime:
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
    case RenderMaterialGraphNodeKind::DistanceCullFade:
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
    case RenderMaterialGraphNodeKind::ObjectRadius:
    case RenderMaterialGraphNodeKind::ObjectBounds:
        AppendIrPin(irNode, irNode.kind, "value", true);
        break;
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::DynamicParameter:
        AppendIrPin(irNode, irNode.kind, "rgba", true);
        AppendIrPin(irNode, irNode.kind, "r", true);
        AppendIrPin(irNode, irNode.kind, "g", true);
        AppendIrPin(irNode, irNode.kind, "b", true);
        AppendIrPin(irNode, irNode.kind, "a", true);
        break;
    case RenderMaterialGraphNodeKind::ScreenPosition:
    case RenderMaterialGraphNodeKind::PixelPosition:
        AppendIrPin(irNode, irNode.kind, "xy", true);
        break;
    case RenderMaterialGraphNodeKind::LocalPosition:
    case RenderMaterialGraphNodeKind::ObjectPosition:
    case RenderMaterialGraphNodeKind::WorldPosition:
        AppendIrPin(irNode, irNode.kind, "xyz", true);
        break;
    }
}

void AppendIrPins(RenderMaterialGraphIrNode& irNode, const RenderMaterialGraphNode& node) {
    if (node.kind == RenderMaterialGraphNodeKind::CustomCode) {
        AppendCustomCodePins(irNode, node);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        AppendMaterialFunctionCallPins(irNode, node);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::Reroute ||
        node.kind == RenderMaterialGraphNodeKind::CompositeInput ||
        node.kind == RenderMaterialGraphNodeKind::CompositeOutput) {
        AppendIrPin(irNode, node, "input", false);
        AppendIrPin(irNode, node, "output", true);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionInput) {
        AppendIrPin(irNode, node, "value", true);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
        AppendIrPin(irNode, node, "value", false);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration) {
        AppendIrPin(irNode, node, "input", false);
        return;
    }
    if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage) {
        AppendIrPin(irNode, node, "output", true);
        return;
    }
    AppendIrPins(irNode);
}

[[nodiscard]] std::uint32_t DiagnosticPinId(const RenderMaterialGraphDocument& graph, const RenderMaterialGraphDiagnostic& diagnostic) noexcept {
    if (diagnostic.pinId != 0U || diagnostic.nodeId == 0U || diagnostic.pin.empty()) {
        return diagnostic.pinId;
    }
    const RenderMaterialGraphNode* node = FindRenderMaterialGraphNode(graph, diagnostic.nodeId);
    if (node == nullptr) {
        return 0U;
    }
    std::uint32_t pinId = RenderMaterialGraphStablePinId(*node, diagnostic.pin, false);
    if (pinId == 0U) {
        pinId = RenderMaterialGraphStablePinId(*node, diagnostic.pin, true);
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
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::MaterialAttributes:
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
    if (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
    }
    if (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float2) {
        return "(" + expression + ").xy";
    }
    if (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float4) {
        return expression;
    }
    if (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
    }
    if (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Float3) {
        return "vec3(" + expression + ", 0.0)";
    }
    if (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Float4) {
        return "vec4(" + expression + ", 0.0, 1.0)";
    }
    if (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Color) {
        return "vec4(" + expression + ", 0.0, 1.0)";
    }
    if (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
    }
    if (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Float2) {
        return "(" + expression + ").xy";
    }
    if (from == RenderMaterialGraphPinType::Normal && to == RenderMaterialGraphPinType::Float) {
        return "(" + expression + ").x";
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
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float2) {
        return "(" + expression + ").xy";
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
    if (from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Float) {
        return "((" + expression + ") ? 1.0 : 0.0)";
    }
    if (from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Float4) {
        return "vec4_splat((" + expression + ") ? 1.0 : 0.0)";
    }
    if (from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Color) {
        return "vec4_splat((" + expression + ") ? 1.0 : 0.0)";
    }
    if (from == RenderMaterialGraphPinType::Float && to == RenderMaterialGraphPinType::Bool) {
        return "((" + expression + ") != 0.0)";
    }
    if (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Bool) {
        return "(((" + expression + ").x) != 0.0)";
    }
    return expression;
}

struct GraphCodegen {
    const RenderMaterialGraphDocument& graph;
    const RenderMaterialGraphBuildContext& context;
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics;
    std::vector<std::uint32_t> stack;
    std::unordered_map<std::uint32_t, std::uint32_t> fanOut;
    std::unordered_map<std::uint32_t, std::string> emittedTemp;
    std::unordered_map<std::string, std::string> customOutputTemps;
    std::unordered_set<std::uint32_t> emittedCustomFunctions;
    std::vector<std::pair<std::uint32_t, std::string>> functionDefinitions;
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
    case RenderMaterialGraphPinType::Bool:
        return "bool";
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
        return "vec4";
    }
    return "vec4";
}

[[nodiscard]] RenderMaterialGraphPinType GraphNodeCanonicalType(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return RenderMaterialGraphPinType::Color;
    case RenderMaterialGraphNodeKind::CustomCode:
        return RenderMaterialGraphPinType::Float4;
    case RenderMaterialGraphNodeKind::ConstantBool:
        return RenderMaterialGraphPinType::Bool;
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return RenderMaterialGraphPinType::Float4;
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
    case RenderMaterialGraphNodeKind::ConstantBool:
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
    case RenderMaterialGraphNodeKind::ObjectOrientation:
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
    case RenderMaterialGraphNodeKind::ViewProperty:
    case RenderMaterialGraphNodeKind::ViewSize:
    case RenderMaterialGraphNodeKind::TwoSidedSign:
    case RenderMaterialGraphNodeKind::SceneDepth:
    case RenderMaterialGraphNodeKind::PixelDepth:
    case RenderMaterialGraphNodeKind::CameraDepthFade:
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
    case RenderMaterialGraphNodeKind::DepthFade:
    case RenderMaterialGraphNodeKind::CustomCode:
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput:
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
    const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(*fromNode, link->fromPin, true);
    return CoerceExpression(
        CompileNodeOutputExpression(cg, *fromNode, link->fromPin),
        fromType,
        expectedType);
}

[[nodiscard]] std::string CompileNamedRerouteUsageExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    const std::string key = NamedRerouteKey(node);
    const RenderMaterialGraphPinType type = PassThroughPinType(node);
    if (key.empty()) {
        AddGraphDiagnostic(
            cg.diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            "output",
            "NamedRerouteUsage requires a stable reroute name.");
        return DefaultExpressionForType(type);
    }

    const RenderMaterialGraphNode* declaration = nullptr;
    std::uint32_t declarationCount = 0U;
    for (const RenderMaterialGraphNode& candidate : cg.graph.nodes) {
        if (candidate.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration &&
            NamedRerouteKey(candidate) == key) {
            declaration = &candidate;
            ++declarationCount;
        }
    }
    if (declarationCount != 1U || declaration == nullptr) {
        AddGraphDiagnostic(
            cg.diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            "output",
            declarationCount == 0U
                ? "NamedRerouteUsage '" + key + "' has no matching declaration."
                : "NamedRerouteUsage '" + key + "' matches multiple declarations.");
        return DefaultExpressionForType(type);
    }
    if (PassThroughPinType(*declaration) != type) {
        AddGraphDiagnostic(
            cg.diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::TypeMismatch,
            node.id,
            0U,
            "output",
            "NamedRerouteUsage '" + key + "' type does not match its declaration.");
        return DefaultExpressionForType(type);
    }
    return CompileInputExpression(cg, *declaration, "input", type, DefaultExpressionForType(type));
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
        .pinId = RenderMaterialGraphStablePinId(node, pin, true),
        .pin = std::string{ pin },
        .message = std::move(message),
    });
}

[[nodiscard]] std::string ParameterUniformName(const RenderMaterialGraphNode& node, std::string_view suffix) {
    return "u_" + SanitizeShaderIdentifier(StableParameterId(node), "parameter" + std::to_string(node.id)) + std::string{ suffix };
}

[[nodiscard]] std::string CollectionParameterUniformName(const RenderMaterialGraphNode& node) {
    const std::uint64_t collectionAssetId = RenderMaterialGraphCollectionAssetId(node);
    const std::string key = std::to_string(collectionAssetId) + "_" + StableParameterId(node);
    return "u_mpc_" + SanitizeShaderIdentifier(key, "collection" + std::to_string(node.id));
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

[[nodiscard]] std::array<float, 4U> DynamicParameterDefaultValue(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar: {
        const float value = values.empty() ? 0.0F : values[0];
        return { value, 0.0F, 0.0F, 0.0F };
    }
    case RenderMaterialGraphNodeKind::ParameterVector: {
        const float x = values.size() > 0U ? values[0] : 0.0F;
        const float y = values.size() > 1U ? values[1] : x;
        const float z = values.size() > 2U ? values[2] : y;
        return { x, y, z, 0.0F };
    }
    case RenderMaterialGraphNodeKind::ParameterColor: {
        const float r = values.size() > 0U ? values[0] : 0.0F;
        const float g = values.size() > 1U ? values[1] : r;
        const float b = values.size() > 2U ? values[2] : g;
        const float a = values.size() > 3U ? values[3] : 1.0F;
        return { r, g, b, a };
    }
    default:
        return {};
    }
}

[[nodiscard]] std::string_view SamplerFilterName(RenderMaterialGraphSamplerFilter filter) noexcept {
    return filter == RenderMaterialGraphSamplerFilter::Point ? "Point" : "Linear";
}

[[nodiscard]] std::string_view SamplerWrapName(RenderMaterialGraphSamplerWrap wrap) noexcept {
    switch (wrap) {
    case RenderMaterialGraphSamplerWrap::Clamp:
        return "Clamp";
    case RenderMaterialGraphSamplerWrap::Mirror:
        return "Mirror";
    case RenderMaterialGraphSamplerWrap::Repeat:
        return "Repeat";
    }
    return "Repeat";
}

[[nodiscard]] bool IsTextureObjectNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsTextureSampleNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool NormalUnpackFeedsSurfaceNormal(const RenderMaterialGraphDocument& graph, std::uint32_t normalUnpackNodeId) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.fromNodeId != normalUnpackNodeId ||
            link.fromPin != "normal" ||
            link.toPin != "normal") {
            continue;
        }
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (toNode != nullptr && toNode->kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool TextureSampleFeedsSurfaceNormal(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t textureSampleNodeId) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.fromNodeId != textureSampleNodeId ||
            link.fromPin != "color" ||
            link.toPin != "color") {
            continue;
        }
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (toNode != nullptr &&
            toNode->kind == RenderMaterialGraphNodeKind::NormalUnpack &&
            NormalUnpackFeedsSurfaceNormal(graph, toNode->id)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool TextureObjectFeedsSurfaceNormal(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t textureObjectNodeId) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.fromNodeId != textureObjectNodeId ||
            link.fromPin != "texture" ||
            link.toPin != "texture") {
            continue;
        }
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (toNode != nullptr &&
            IsTextureSampleNode(toNode->kind) &&
            TextureSampleFeedsSurfaceNormal(graph, toNode->id)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool TextureNodeFeedsSurfaceNormal(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node) noexcept {
    if (IsTextureSampleNode(node.kind)) {
        return TextureSampleFeedsSurfaceNormal(graph, node.id);
    }
    if (IsTextureObjectNode(node.kind)) {
        return TextureObjectFeedsSurfaceNormal(graph, node.id);
    }
    return false;
}

[[nodiscard]] RenderMaterialGraphTextureDimension TextureDimensionForNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
        return RenderMaterialGraphTextureDimension::TextureCube;
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
        return RenderMaterialGraphTextureDimension::Texture3D;
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return RenderMaterialGraphTextureDimension::Texture2DArray;
    default:
        return RenderMaterialGraphTextureDimension::Texture2D;
    }
}

[[nodiscard]] RenderMaterialGraphPinType TexturePinTypeForDimension(RenderMaterialGraphTextureDimension dimension) noexcept {
    switch (dimension) {
    case RenderMaterialGraphTextureDimension::TextureCube:
        return RenderMaterialGraphPinType::TextureCube;
    case RenderMaterialGraphTextureDimension::Texture3D:
        return RenderMaterialGraphPinType::Texture3D;
    case RenderMaterialGraphTextureDimension::Texture2DArray:
        return RenderMaterialGraphPinType::Texture2DArray;
    case RenderMaterialGraphTextureDimension::Texture2D:
        return RenderMaterialGraphPinType::Texture2D;
    }
    return RenderMaterialGraphPinType::Texture2D;
}

[[nodiscard]] std::string_view TextureSamplerMacro(RenderMaterialGraphTextureDimension dimension) noexcept {
    switch (dimension) {
    case RenderMaterialGraphTextureDimension::TextureCube:
        return "SAMPLERCUBE";
    case RenderMaterialGraphTextureDimension::Texture3D:
        return "SAMPLER3D";
    case RenderMaterialGraphTextureDimension::Texture2DArray:
        return "SAMPLER2DARRAY";
    case RenderMaterialGraphTextureDimension::Texture2D:
        return "SAMPLER2D";
    }
    return "SAMPLER2D";
}

[[nodiscard]] bool SceneTextureReadsDepth(const RenderMaterialGraphNode& node) noexcept {
    return EqualsIgnoreCase(node.parameter.defaultValueHint, "depth") ||
           EqualsIgnoreCase(node.parameter.defaultValueHint, "sceneDepth") ||
           EqualsIgnoreCase(node.parameter.defaultValueHint, "SceneDepth");
}

[[nodiscard]] std::string CompileTextureInputExpression(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    const RenderMaterialGraphTextureDimension expectedDimension = TextureDimensionForNode(node.kind);
    const RenderMaterialGraphLink* link = FindInputLink(graph, node.id, "texture");
    if (link == nullptr) {
        return ParameterUniformName(node, "_texture");
    }
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link->fromNodeId);
    if (fromNode == nullptr || !IsTextureObjectNode(fromNode->kind) || link->fromPin != "texture") {
        AddShaderGenerationDiagnostic(diagnostics, node, "texture", "Texture sample input must be connected to a texture object parameter.");
        return "u_missingTexture";
    }
    if (TextureDimensionForNode(fromNode->kind) != expectedDimension) {
        AddShaderGenerationDiagnostic(diagnostics, node, "texture", "Texture sample dimension does not match the connected texture object.");
        return "u_missingTexture";
    }
    return ParameterUniformName(*fromNode, "_texture");
}

void EmitCustomCodeFunction(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    if (node.kind != RenderMaterialGraphNodeKind::CustomCode ||
        !cg.emittedCustomFunctions.insert(node.id).second) {
        return;
    }

    std::string definition;
    if (!node.customCode.defines.empty()) {
        definition += node.customCode.defines;
        if (definition.empty() || definition.back() != '\n') {
            definition += '\n';
        }
    }
    if (!node.customCode.includes.empty()) {
        definition += node.customCode.includes;
        if (definition.empty() || definition.back() != '\n') {
            definition += '\n';
        }
    }
    definition += GraphCodegenGlslType(node.customCode.outputType) + " " + CustomCodeFunctionName(node) + "(";
    bool first = true;
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.inputs) {
        if (!first) {
            definition += ", ";
        }
        first = false;
        definition += GraphCodegenGlslType(pin.type) + " " + pin.name;
    }
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.outputs) {
        if (!first) {
            definition += ", ";
        }
        first = false;
        definition += "inout " + GraphCodegenGlslType(pin.type) + " " + pin.name;
    }
    definition += ") {\n";
    definition += node.customCode.body;
    if (definition.empty() || definition.back() != '\n') {
        definition += '\n';
    }
    definition += "}\n\n";
    cg.functionDefinitions.push_back({ node.id, std::move(definition) });
}

[[nodiscard]] std::string EmitCustomCodeCall(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    if (const auto it = cg.emittedTemp.find(node.id); it != cg.emittedTemp.end()) {
        return it->second;
    }

    EmitCustomCodeFunction(cg, node);
    const std::string id = std::to_string(node.id);
    std::vector<std::string> arguments;
    arguments.reserve(node.customCode.inputs.size() + node.customCode.outputs.size());
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.inputs) {
        arguments.push_back(CompileInputExpression(cg, node, pin.name, pin.type, DefaultExpressionForType(pin.type)));
    }
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.outputs) {
        const std::string temp = CustomCodeTempName(node, pin.name);
        cg.statements += "    " + GraphCodegenGlslType(pin.type) + " " + temp + " = " + DefaultExpressionForType(pin.type) + ";\n";
        cg.customOutputTemps[CustomCodeOutputTempKey(node.id, pin.name)] = temp;
        arguments.push_back(temp);
    }

    const std::string valueTemp = "custom" + id + "_value";
    cg.statements += "    " + GraphCodegenGlslType(node.customCode.outputType) + " " + valueTemp + " = " + CustomCodeFunctionName(node) + "(";
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        if (index != 0U) {
            cg.statements += ", ";
        }
        cg.statements += arguments[index];
    }
    cg.statements += ");\n";
    cg.emittedTemp.emplace(node.id, valueTemp);
    cg.customOutputTemps[CustomCodeOutputTempKey(node.id, "value")] = valueTemp;
    return valueTemp;
}

void AppendCustomCodeFunctionDefinitions(
    std::string& source,
    const std::vector<std::pair<std::uint32_t, std::string>>& definitions,
    std::unordered_set<std::uint32_t>& emittedDefinitions) {
    for (const auto& [nodeId, definition] : definitions) {
        if (emittedDefinitions.insert(nodeId).second) {
            source += definition;
        }
    }
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
        "    " + v + ".tangentOutput = vec3(1.0, 0.0, 0.0);\n";
}

// MAT-39: parse a compile-time boolean from a node's value hint.
[[nodiscard]] bool ParseStaticBoolHint(std::string_view hint) noexcept {
    return EqualsIgnoreCase(hint, "true") || hint == "1";
}

[[nodiscard]] std::string_view RenderMaterialGraphQualityLevelPinName(RenderMaterialGraphQualityLevel level) noexcept {
    switch (level) {
    case RenderMaterialGraphQualityLevel::Low: return "low";
    case RenderMaterialGraphQualityLevel::Medium: return "med";
    case RenderMaterialGraphQualityLevel::High: return "high";
    case RenderMaterialGraphQualityLevel::Epic: return "epic";
    }
    return "high";
}

[[nodiscard]] std::string_view RenderMaterialGraphFeatureLevelPinName(RenderMaterialGraphFeatureLevel level) noexcept {
    switch (level) {
    case RenderMaterialGraphFeatureLevel::Es3: return "es3";
    case RenderMaterialGraphFeatureLevel::Sm5: return "sm5";
    case RenderMaterialGraphFeatureLevel::Sm6: return "sm6";
    }
    return "sm5";
}

[[nodiscard]] std::string_view RenderMaterialGraphShadingPathPinName(RenderMaterialGraphShadingPath path) noexcept {
    switch (path) {
    case RenderMaterialGraphShadingPath::Forward: return "forward";
    case RenderMaterialGraphShadingPath::ForwardPlus: return "forwardPlus";
    case RenderMaterialGraphShadingPath::Deferred: return "deferred";
    }
    return "forward";
}

[[nodiscard]] std::string_view RenderMaterialGraphShaderStagePinName(RenderMaterialGraphShaderStage stage) noexcept {
    switch (stage) {
    case RenderMaterialGraphShaderStage::Fragment: return "fragment";
    case RenderMaterialGraphShaderStage::Vertex: return "vertex";
    }
    return "fragment";
}

[[nodiscard]] std::string_view SelectedStaticVariantSwitchPin(
    const RenderMaterialGraphBuildContext& context,
    RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::QualitySwitch:
        return RenderMaterialGraphQualityLevelPinName(context.qualityLevel);
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
        return RenderMaterialGraphFeatureLevelPinName(context.featureLevel);
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
        return RenderMaterialGraphShadingPathPinName(context.shadingPath);
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return RenderMaterialGraphShaderStagePinName(context.shaderStage);
    default:
        return "result";
    }
}

[[nodiscard]] bool IsStaticVariantSwitchNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::StaticSwitch:
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool GraphContainsNodeKind(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphNodeKind kind) noexcept {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == kind) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string CompileStaticVariantSwitchExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    const std::string_view selectedPin = SelectedStaticVariantSwitchPin(cg.context, node.kind);
    return CompileInputExpression(cg, node, selectedPin, RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
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

[[nodiscard]] std::string ViewPropertyExpression(std::string_view hint) {
    if (EqualsIgnoreCase(hint, "invViewSize") || EqualsIgnoreCase(hint, "inverseViewSize") || EqualsIgnoreCase(hint, "viewInvSize")) {
        return "(vec2(1.0, 1.0) / max(ctx.viewSize, vec2(1.0, 1.0)))";
    }
    if (EqualsIgnoreCase(hint, "screenPosition") || EqualsIgnoreCase(hint, "viewportUV") || EqualsIgnoreCase(hint, "screenUV")) {
        return "ctx.screenPosition";
    }
    if (EqualsIgnoreCase(hint, "pixelPosition") || EqualsIgnoreCase(hint, "viewportPixelPosition") || EqualsIgnoreCase(hint, "screenPixelPosition")) {
        return "(ctx.screenPosition * ctx.viewSize)";
    }
    return "ctx.viewSize";
}

struct TextureCoordinateHint {
    float uTile = 1.0F;
    float vTile = 1.0F;
    bool useUv1 = false;
};

[[nodiscard]] TextureCoordinateHint ParseTextureCoordinateHint(std::string_view hint) {
    TextureCoordinateHint result{};
    if (hint.empty() || EqualsIgnoreCase(hint, "0") || EqualsIgnoreCase(hint, "uv0")) {
        return result;
    }
    if (EqualsIgnoreCase(hint, "1") || EqualsIgnoreCase(hint, "uv1")) {
        result.useUv1 = true;
        return result;
    }

    const std::vector<float> values = ParseDefaultNumbers(hint);
    if (!values.empty()) {
        result.uTile = values[0];
        result.vTile = values.size() > 1U ? values[1] : result.uTile;
        result.useUv1 = values.size() > 2U && values[2] >= 0.5F;
    }
    return result;
}

std::string CompileNodeBaseExpression(GraphCodegen& cg, const RenderMaterialGraphNode& node) {
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::Reroute:
    case RenderMaterialGraphNodeKind::CompositeInput:
    case RenderMaterialGraphNodeKind::CompositeOutput: {
        const RenderMaterialGraphPinType type = PassThroughPinType(node);
        return CompileInputExpression(cg, node, "input", type, DefaultExpressionForType(type));
    }
    case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return CompileNamedRerouteUsageExpression(cg, node);
    case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return DefaultExpressionForType(PassThroughPinType(node));
    case RenderMaterialGraphNodeKind::StaticBoolParameter:
        // A compile-time boolean baked as a literal so it participates in the shader source / variant key.
        return ParseStaticBoolHint(node.parameter.defaultValueHint) ? "1.0" : "0.0";
    case RenderMaterialGraphNodeKind::ConstantBool:
        return ParseStaticBoolHint(node.parameter.defaultValueHint) ? "true" : "false";
    case RenderMaterialGraphNodeKind::StaticSwitch:
        // Only the selected branch is compiled â€” the other subgraph is never emitted (dead-branch elimination).
        return ResolveStaticSwitchSelector(cg.graph, node)
            ? CompileInputExpression(cg, node, "true", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)")
            : CompileInputExpression(cg, node, "false", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
    case RenderMaterialGraphNodeKind::QualitySwitch:
    case RenderMaterialGraphNodeKind::FeatureLevelSwitch:
    case RenderMaterialGraphNodeKind::ShadingPathSwitch:
    case RenderMaterialGraphNodeKind::ShaderStageSwitch:
        return CompileStaticVariantSwitchExpression(cg, node);
    case RenderMaterialGraphNodeKind::StaticComponentMask:
        return StaticComponentMaskExpression(
            CompileInputExpression(cg, node, "input", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)"),
            node.parameter.defaultValueHint);
    case RenderMaterialGraphNodeKind::TextureCoordinate: {
        // MAT-45/MAT-81: tiling-scaled UV from a selectable coordinate set. Legacy hints "0"/"1"
        // mean UV0/UV1; extended hints use "uTile vTile set".
        const TextureCoordinateHint hint = ParseTextureCoordinateHint(node.parameter.defaultValueHint);
        return std::string{ hint.useUv1 ? "ctx.uv1" : "ctx.uv0" } + " * vec2(" + FloatLiteral(hint.uTile) + ", " + FloatLiteral(hint.vTile) + ")";
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
    case RenderMaterialGraphNodeKind::ObjectOrientation:
        return "ctx.objectOrientation";
    case RenderMaterialGraphNodeKind::PreSkinnedPosition:
        return "ctx.preSkinnedPosition";
    case RenderMaterialGraphNodeKind::PreSkinnedNormal:
        return "ctx.preSkinnedNormal";
    case RenderMaterialGraphNodeKind::ViewSize:
        return "ctx.viewSize";
    case RenderMaterialGraphNodeKind::ViewProperty:
        return ViewPropertyExpression(node.parameter.defaultValueHint);
    case RenderMaterialGraphNodeKind::TwoSidedSign:
        return "ctx.twoSidedSign";
    case RenderMaterialGraphNodeKind::SceneDepth:
        // MAT-80/#18b: the opaque scene device depth at this fragment's screen position.
        return "texture2D(s_kbSceneDepth, ctx.screenPosition).x";
    case RenderMaterialGraphNodeKind::PixelDepth:
        return "ctx.fragmentDepth";
    case RenderMaterialGraphNodeKind::CameraDepthFade: {
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float defaultFadeLength = values.size() > 0U ? std::max(values[0], 0.0001F) : 1.0F;
        const float defaultFadeOffset = values.size() > 1U ? values[1] : 0.0F;
        const std::string fadeLength = CompileInputExpression(
            cg,
            node,
            "fadeLength",
            RenderMaterialGraphPinType::Float,
            FloatLiteral(defaultFadeLength));
        const std::string fadeOffset = CompileInputExpression(
            cg,
            node,
            "fadeOffset",
            RenderMaterialGraphPinType::Float,
            FloatLiteral(defaultFadeOffset));
        return "clamp((distance(ctx.cameraPosition, ctx.worldPos) - (" + fadeOffset + ")) / max(" + fadeLength + ", 0.0001), 0.0, 1.0)";
    }
    case RenderMaterialGraphNodeKind::SceneColor: {
        const std::string uv = CompileInputExpression(cg, node, "uv", RenderMaterialGraphPinType::Float2, "ctx.screenPosition");
        return "texture2D(s_kbSceneColor, " + uv + ")";
    }
    case RenderMaterialGraphNodeKind::SceneTexture: {
        const std::string uv = CompileInputExpression(cg, node, "uv", RenderMaterialGraphPinType::Float2, "ctx.screenPosition");
        return SceneTextureReadsDepth(node)
            ? "vec4_splat(texture2D(s_kbSceneDepth, " + uv + ").x)"
            : "texture2D(s_kbSceneColor, " + uv + ")";
    }
    case RenderMaterialGraphNodeKind::DepthFade: {
        // MAT-80/#18b: soft fade (0 at the opaque surface, 1 further in front). abs() of the device-depth
        // separation is projection-convention robust; the transparent fragment already passed the depth
        // test so it lies in front of the sampled opaque depth.
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float defaultFadeDistance = values.size() > 0U ? std::max(values[0], 0.0001F) : 0.01F;
        const std::string fadeDistance = CompileInputExpression(
            cg,
            node,
            "fadeDistance",
            RenderMaterialGraphPinType::Float,
            FloatLiteral(defaultFadeDistance));
        return "clamp(abs(texture2D(s_kbSceneDepth, ctx.screenPosition).x - ctx.fragmentDepth) / max(" + fadeDistance + ", 0.0001), 0.0, 1.0)";
    }
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
        const std::string input = CompileInputExpression(cg, node, "input", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
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
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float axisX = values.size() > 0U ? values[0] : 0.0F;
        const float axisY = values.size() > 1U ? values[1] : 0.0F;
        const float axisZ = values.size() > 2U ? values[2] : 1.0F;
        const float angleDefault = values.size() > 3U ? values[3] : 0.0F;
        const std::string axisDefault =
            "vec3(" + FloatLiteral(axisX) + ", " + FloatLiteral(axisY) + ", " + FloatLiteral(axisZ) + ")";
        const std::string axis = CompileInputExpression(cg, node, "axis", RenderMaterialGraphPinType::Float3, axisDefault);
        const std::string angle = CompileInputExpression(cg, node, "angle", RenderMaterialGraphPinType::Float, FloatLiteral(angleDefault));
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
        cg.statements += "    " + tmp + ".alphaClipThreshold = " + CompileInputExpression(cg, node, "alphaClipThreshold", RenderMaterialGraphPinType::Float, "0.5") + ";\n";
        cg.statements += "    " + tmp + ".specular = " + CompileInputExpression(cg, node, "specular", RenderMaterialGraphPinType::Float, "0.5") + ";\n";
        cg.statements += "    " + tmp + ".tangentOutput = " + CompileInputExpression(cg, node, "tangentOutput", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)") + ";\n";
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
        cg.statements += "    " + tmp + ".tangentOutput = normalize(mix((" + a + ").tangentOutput, (" + b + ").tangentOutput, " + fv + "));\n";
        cg.emittedTemp.emplace(node.id, tmp);
        return tmp;
    }
    case RenderMaterialGraphNodeKind::GetMaterialAttributes: {
        // GetMaterialAttributes reads channels out of an attribute set â€” the set itself is the source expression.
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
        if (FindInputLink(cg.graph, node.id, "alphaClipThreshold") != nullptr) {
            cg.statements += "    " + tmp + ".alphaClipThreshold = " + CompileInputExpression(cg, node, "alphaClipThreshold", RenderMaterialGraphPinType::Float, "0.5") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "specular") != nullptr) {
            cg.statements += "    " + tmp + ".specular = " + CompileInputExpression(cg, node, "specular", RenderMaterialGraphPinType::Float, "0.5") + ";\n";
        }
        if (FindInputLink(cg.graph, node.id, "tangentOutput") != nullptr) {
            cg.statements += "    " + tmp + ".tangentOutput = " + CompileInputExpression(cg, node, "tangentOutput", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)") + ";\n";
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
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return CollectionParameterUniformName(node);
    case RenderMaterialGraphNodeKind::Add:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            " + " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Subtract:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            " - " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Multiply:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            " * " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Divide:
        return "(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            " / max(abs(" +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            "), vec4_splat(0.0001)))";
    case RenderMaterialGraphNodeKind::Power:
        return "pow(max(" +
            CompileInputExpression(cg, node, "base", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", vec4_splat(0.0)), " +
            CompileInputExpression(cg, node, "exponent", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::OneMinus:
        return "(vec4_splat(1.0) - " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Absolute:
        return "abs(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Minimum:
        return "min(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Maximum:
        return "max(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Saturate:
        return "clamp(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", vec4_splat(0.0), vec4_splat(1.0))";
    case RenderMaterialGraphNodeKind::Floor:
        return "floor(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Ceil:
        return "ceil(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Fraction:
        return "fract(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::SquareRoot:
        return "sqrt(max(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", vec4_splat(0.0)))";
    case RenderMaterialGraphNodeKind::Sine:
        return "sin(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Cosine:
        return "cos(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Exponential:
        return "exp(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Exponential2:
        return "exp2(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Logarithm:
        return "log(max(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") + ", vec4_splat(0.0001)))";
    case RenderMaterialGraphNodeKind::Logarithm2:
        return "log2(max(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") + ", vec4_splat(0.0001)))";
    case RenderMaterialGraphNodeKind::SrgbToLinear: {
        // Gamma-decode the RGB channels (alpha is linear); pow keeps the standard 2.2 approximation.
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "vec4(pow(max((" + v + ").rgb, vec3(0.0, 0.0, 0.0)), vec3(2.2, 2.2, 2.2)), (" + v + ").a)";
    }
    case RenderMaterialGraphNodeKind::LinearToSrgb: {
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "vec4(pow(max((" + v + ").rgb, vec3(0.0, 0.0, 0.0)), vec3(0.454545, 0.454545, 0.454545)), (" + v + ").a)";
    }
    case RenderMaterialGraphNodeKind::Logarithm10:
        // log10(x) = ln(x) * (1/ln(10)); clamp the argument away from zero to keep the result finite.
        return "(log(max(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ", vec4_splat(0.0001))) * vec4_splat(0.434294))";
    case RenderMaterialGraphNodeKind::HsvToRgb: {
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "vec4(kbHsvToRgb((" + v + ").rgb), (" + v + ").a)";
    }
    case RenderMaterialGraphNodeKind::RgbToHsv: {
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "vec4(kbRgbToHsv((" + v + ").rgb), (" + v + ").a)";
    }
    case RenderMaterialGraphNodeKind::DeriveNormalZ: {
        // Reconstruct the tangent-space normal's Z from its XY and renormalize (UE DeriveNormalZ).
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "vec4(normalize(vec3((" + v + ").xy, sqrt(clamp(1.0 - dot((" + v + ").xy, (" + v + ").xy), 0.0, 1.0)))), 1.0)";
    }
    case RenderMaterialGraphNodeKind::Fmod: {
        const std::string a = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        const std::string b = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)");
        // Guard each component's divisor so a zero denominator falls back to 1.0 instead of producing NaN.
        return "mod(" + a + ", mix(" + b + ", vec4_splat(1.0), step(abs(" + b + "), vec4_splat(0.0001))))";
    }
    case RenderMaterialGraphNodeKind::InverseLerp: {
        const std::string a = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        const std::string b = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)");
        const std::string t = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        // (value - a) / (b - a) with a guarded denominator so coincident endpoints don't divide by zero.
        const std::string den = "(" + b + " - " + a + ")";
        return "((" + t + " - " + a + ") / mix(" + den + ", vec4_splat(1.0), step(abs(" + den + "), vec4_splat(0.0001))))";
    }
    case RenderMaterialGraphNodeKind::PartialDerivativeX:
        // bgfx maps dFdx to ddx/ddFdx per backend; valid in the fragment stage where the graph FS runs.
        return "dFdx(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::PartialDerivativeY:
        return "dFdy(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::SphereMask: {
        // MAT-50 SphereMask(A, B, Radius, Hardness): a soft radial mask of the distance between A and B.
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float radius = values.size() > 0U && values[0] > 0.0001F ? values[0] : 1.0F;
        float hardness = values.size() > 1U ? values[1] : 0.5F;
        hardness = hardness < 0.0F ? 0.0F : (hardness > 0.999F ? 0.999F : hardness);
        const std::string a = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)");
        const std::string b = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)");
        const std::string inner = FloatLiteral(radius * (1.0F - hardness));
        return "vec4_splat(1.0 - smoothstep(" + inner + ", " + FloatLiteral(radius) + ", distance(" + a + ", " + b + ")))";
    }
    case RenderMaterialGraphNodeKind::BlackBody:
        // MAT-50: map a Kelvin temperature (scalar) to the Planckian-locus RGB via the shared prelude helper.
        return "vec4(kbBlackBody(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float, "6500.0") +
            "), 1.0)";
    case RenderMaterialGraphNodeKind::Noise:
        // MAT-50: smooth value noise of the input position, broadcast to all channels.
        return "vec4(vec3_splat(kbValueNoise((" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float3, "ctx.worldPos") +
            ").xyz)), 1.0)";
    case RenderMaterialGraphNodeKind::VectorNoise:
        // MAT-50: three decorrelated value-noise channels for a vector perturbation field.
        return "vec4(kbVectorNoise((" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float3, "ctx.worldPos") +
            ").xyz), 1.0)";
    case RenderMaterialGraphNodeKind::Sobol:
        return "kbSobol2(" +
            CompileInputExpression(cg, node, "cell", RenderMaterialGraphPinType::Float2, "vec2(0.0, 0.0)") +
            ", " +
            CompileInputExpression(cg, node, "index", RenderMaterialGraphPinType::Float, "0.0") +
            ", " +
            CompileInputExpression(cg, node, "seed", RenderMaterialGraphPinType::Float2, "vec2(0.0, 0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::AppendVector:
        // MAT-50: concatenate a 3-component vector with a scalar into a float4 (UE's common rgb + a append).
        return "vec4((" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)") +
            ").xyz, (" +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float, "1.0") +
            "))";
    case RenderMaterialGraphNodeKind::ColorRamp: {
        // MAT-50: map the scalar "value" through a gradient of colour stops (hint = "pos r g b" per stop).
        // Consecutive stops are blended with smoothstep; the shader compiler CSEs the repeated position term.
        const std::vector<float> numbers = ParseDefaultNumbers(node.parameter.defaultValueHint);
        std::vector<std::array<float, 4>> stops;  // { position, r, g, b }
        for (std::size_t i = 0U; i + 3U < numbers.size(); i += 4U) {
            stops.push_back({ numbers[i], numbers[i + 1U], numbers[i + 2U], numbers[i + 3U] });
        }
        if (stops.size() < 2U) {
            stops = { { 0.0F, 0.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F, 1.0F } };
        }
        const std::string t = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float, "0.0");
        const auto colorLiteral = [](const std::array<float, 4>& stop) {
            return "vec3(" + FloatLiteral(stop[1]) + ", " + FloatLiteral(stop[2]) + ", " + FloatLiteral(stop[3]) + ")";
        };
        std::string expr = colorLiteral(stops[0]);
        for (std::size_t i = 1U; i < stops.size(); ++i) {
            expr = "mix(" + expr + ", " + colorLiteral(stops[i]) + ", smoothstep(" +
                FloatLiteral(stops[i - 1U][0]) + ", " + FloatLiteral(stops[i][0]) + ", " + t + "))";
        }
        return "vec4(" + expr + ", 1.0)";
    }
    case RenderMaterialGraphNodeKind::AntialiasedTextureMask: {
        // MAT-50: threshold the scalar "value" into a 0/1 mask whose edge is one screen-space pixel wide
        // (fwidth = |dFdx| + |dFdy|), so masks stay crisp without shimmering. hint = "threshold".
        const std::vector<float> numbers = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float threshold = numbers.empty() ? 0.5F : numbers[0];
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float, "0.0");
        const std::string width = "(abs(dFdx(" + v + ")) + abs(dFdy(" + v + ")) + 0.00001)";
        return "vec4_splat(smoothstep(" + FloatLiteral(threshold) + " - " + width + ", " + FloatLiteral(threshold) + " + " + width + ", " + v + "))";
    }
    case RenderMaterialGraphNodeKind::Transform:
    case RenderMaterialGraphNodeKind::TransformPosition: {
        // MAT-50/#14: transform the input vector/point between coordinate spaces (hint = "from to", spaces:
        // tangent | world | view). Tangent<->world uses the interpolated TBN; world<->view uses the bgfx
        // view matrix. Positions carry translation (w=1), directions do not (w=0).
        const bool isPosition = node.kind == RenderMaterialGraphNodeKind::TransformPosition;
        std::istringstream spaceStream{ node.parameter.defaultValueHint };
        std::string fromSpace;
        std::string toSpace;
        spaceStream >> fromSpace >> toSpace;
        if (fromSpace.empty()) fromSpace = "tangent";
        if (toSpace.empty()) toSpace = "world";
        const std::string w = isPosition ? "1.0" : "0.0";
        const std::string tbn = "mat3(ctx.tangent, ctx.bitangent, ctx.normal)";
        const std::string v = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const auto toWorld = [&](const std::string& e, const std::string& space) -> std::string {
            if (EqualsIgnoreCase(space, "tangent")) return "mul(" + tbn + ", (" + e + "))";
            if (EqualsIgnoreCase(space, "view")) return "(mul(u_invView, vec4((" + e + "), " + w + ")).xyz)";
            return e;  // world (or unknown) is the canonical space
        };
        const auto fromWorld = [&](const std::string& e, const std::string& space) -> std::string {
            if (EqualsIgnoreCase(space, "tangent")) return "mul((" + e + "), " + tbn + ")";
            if (EqualsIgnoreCase(space, "view")) return "(mul(u_view, vec4((" + e + "), " + w + ")).xyz)";
            return e;
        };
        return "vec4(" + fromWorld(toWorld(v, fromSpace), toSpace) + ", 1.0)";
    }
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
            CompileInputExpression(cg, node, "edge", RenderMaterialGraphPinType::Float4, "vec4_splat(0.5)") +
            ", " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::SmoothStep:
        return "smoothstep(" +
            CompileInputExpression(cg, node, "min", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "max", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ", " +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::If: {
        const std::string lhs = CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float, "0.0");
        const std::string rhs = CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float, "0.0");
        const std::string less = CompileInputExpression(cg, node, "less", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        const std::string equal = CompileInputExpression(cg, node, "equal", RenderMaterialGraphPinType::Float4, "vec4_splat(0.5)");
        const std::string greater = CompileInputExpression(cg, node, "greater", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)");
        return "((" + lhs + " > " + rhs + ") ? " + greater + " : ((abs(" + lhs + " - " + rhs + ") <= 0.0001) ? " + equal + " : " + less + "))";
    }
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
        return "kbSwitch4(" +
            CompileInputExpression(cg, node, "index", RenderMaterialGraphPinType::Float, "0.0") +
            ", " +
            CompileInputExpression(cg, node, "default", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "case0", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "case1", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "case2", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "case3", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Desaturate: {
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float defaultFraction = values.size() > 0U ? values[0] : 1.0F;
        const std::string color = CompileInputExpression(cg, node, "color", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)");
        const std::string fraction = CompileInputExpression(cg, node, "fraction", RenderMaterialGraphPinType::Float, FloatLiteral(defaultFraction));
        const std::string luma = "dot((" + color + ").rgb, vec3(0.299, 0.587, 0.114))";
        return "mix(" + color + ", vec4(vec3(" + luma + "), (" + color + ").a), clamp(" + fraction + ", 0.0, 1.0))";
    }
    case RenderMaterialGraphNodeKind::Fresnel: {
        const std::vector<float> values = ParseDefaultNumbers(node.parameter.defaultValueHint);
        const float defaultExponent = values.size() > 0U ? std::max(values[0], 0.0001F) : 5.0F;
        const float defaultBase = values.size() > 1U ? values[1] : 0.0F;
        const std::string normal = CompileInputExpression(cg, node, "normal", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const std::string view = CompileInputExpression(cg, node, "view", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 1.0)");
        const std::string exponent = CompileInputExpression(cg, node, "exponent", RenderMaterialGraphPinType::Float, FloatLiteral(defaultExponent));
        const std::string base = CompileInputExpression(cg, node, "base", RenderMaterialGraphPinType::Float, FloatLiteral(defaultBase));
        const std::string facing = "clamp(dot(normalize(" + normal + "), normalize(" + view + ")), 0.0, 1.0)";
        return "mix(pow(1.0 - " + facing + ", max(" + exponent + ", 0.0001)), 1.0, clamp(" + base + ", 0.0, 1.0))";
    }
    case RenderMaterialGraphNodeKind::Negate:
        return "-(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Sign:
        return "sign(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Round:
        return "round(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::Truncate: {
        const std::string value = CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)");
        return "sign(" + value + ") * floor(abs(" + value + "))";
    }
    case RenderMaterialGraphNodeKind::Tangent:
        return "tan(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::ArcSine:
        return "asin(clamp(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ", vec4_splat(-1.0), vec4_splat(1.0)))";
    case RenderMaterialGraphNodeKind::ArcCosine:
        return "acos(clamp(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") + ", vec4_splat(-1.0), vec4_splat(1.0)))";
    case RenderMaterialGraphNodeKind::ArcTangent:
        return "atan(" + CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") + ")";
    case RenderMaterialGraphNodeKind::ArcTangent2:
        return "atan(" +
            CompileInputExpression(cg, node, "y", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "x", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::ArcSineFast:
        return "kbAsinFast(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::ArcCosineFast:
        return "kbAcosFast(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::ArcTangentFast:
        return "kbAtanFast(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return "kbAtan2Fast(" +
            CompileInputExpression(cg, node, "y", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "x", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Clamp:
        return "clamp(" +
            CompileInputExpression(cg, node, "value", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "min", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "max", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
            ")";
    case RenderMaterialGraphNodeKind::Lerp:
        return "mix(" +
            CompileInputExpression(cg, node, "a", RenderMaterialGraphPinType::Float4, "vec4_splat(0.0)") +
            ", " +
            CompileInputExpression(cg, node, "b", RenderMaterialGraphPinType::Float4, "vec4_splat(1.0)") +
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
    case RenderMaterialGraphNodeKind::DeltaTime:
        return "ctx.deltaTime";
    case RenderMaterialGraphNodeKind::DynamicParameter:
        return "ctx.dynamicParameter";
    case RenderMaterialGraphNodeKind::VertexColor:
        return "ctx.vertexColor";
    case RenderMaterialGraphNodeKind::ScreenPosition:
        return "ctx.screenPosition";
    case RenderMaterialGraphNodeKind::PixelPosition:
        return "(ctx.screenPosition * ctx.viewSize)";
    case RenderMaterialGraphNodeKind::LocalPosition:
        return "ctx.localPosition";
    case RenderMaterialGraphNodeKind::ObjectPosition:
        return "ctx.objectPosition";
    case RenderMaterialGraphNodeKind::WorldPosition:
        return "ctx.worldPos";
    case RenderMaterialGraphNodeKind::PerInstanceRandom:
        return "ctx.perInstanceRandom";
    case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
        return "ctx.perInstanceFadeAmount";
    case RenderMaterialGraphNodeKind::DistanceCullFade:
        return "ctx.perInstanceFadeAmount";
    case RenderMaterialGraphNodeKind::PerInstanceCustomData:
        return "ctx.perInstanceCustomData";
    case RenderMaterialGraphNodeKind::ObjectRadius:
        return "ctx.objectRadius";
    case RenderMaterialGraphNodeKind::ObjectBounds:
        return "ctx.objectBounds";
    case RenderMaterialGraphNodeKind::TextureSample:
        return "texture2D(" +
            CompileTextureInputExpression(cg.graph, node, cg.diagnostics) +
            ", " +
            CompileInputExpression(cg, node, "uv", RenderMaterialGraphPinType::Float2, "ctx.uv0") +
            ")";
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        return "textureCube(" +
            CompileTextureInputExpression(cg.graph, node, cg.diagnostics) +
            ", normalize(" +
            CompileInputExpression(cg, node, "direction", RenderMaterialGraphPinType::Float3, "ctx.viewDir") +
            "))";
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        return "texture3D(" +
            CompileTextureInputExpression(cg.graph, node, cg.diagnostics) +
            ", " +
            CompileInputExpression(cg, node, "uvw", RenderMaterialGraphPinType::Float3, "vec3(ctx.uv0, 0.0)") +
            ")";
    case RenderMaterialGraphNodeKind::TextureSample2DArray: {
        const std::string uv = CompileInputExpression(cg, node, "uv", RenderMaterialGraphPinType::Float2, "ctx.uv0");
        const std::string layer = CompileInputExpression(cg, node, "layer", RenderMaterialGraphPinType::Float, "0.0");
        return "texture2DArray(" + CompileTextureInputExpression(cg.graph, node, cg.diagnostics) + ", vec3(" + uv + ", " + layer + "))";
    }
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, "texture", "Texture parameter cannot be emitted as a numeric shader expression without a TextureSample node.");
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node, "texture", true));
    case RenderMaterialGraphNodeKind::CustomCode:
        return EmitCustomCodeCall(cg, node);
    case RenderMaterialGraphNodeKind::FunctionInput:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, "value", "FunctionInput must be inlined before shader generation.");
        return DefaultExpressionForType(FunctionEndpointPinType(node));
    case RenderMaterialGraphNodeKind::FunctionOutput:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, "value", "FunctionOutput must be inlined before shader generation.");
        return DefaultExpressionForType(FunctionEndpointPinType(node));
    case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, {}, "MaterialFunctionCall must be inlined before shader generation.");
        return DefaultExpressionForType(RenderMaterialGraphPinType::Float4);
    case RenderMaterialGraphNodeKind::LayerStack:
        AddShaderGenerationDiagnostic(cg.diagnostics, node, "attributes", "LayerStack must be expanded before shader generation.");
        return DefaultExpressionForType(RenderMaterialGraphPinType::MaterialAttributes);
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
        if (outputPin == "alphaClipThreshold") return "(" + baseRef + ").alphaClipThreshold";
        if (outputPin == "specular") return "(" + baseRef + ").specular";
        if (outputPin == "tangentOutput") return "(" + baseRef + ").tangentOutput";
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
    case RenderMaterialGraphNodeKind::ConstantVector:
        if (outputPin == "r") {
            return "(" + baseRef + ").x";
        }
        if (outputPin == "g") {
            return "(" + baseRef + ").y";
        }
        if (outputPin == "b") {
            return "(" + baseRef + ").z";
        }
        return baseRef;
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::SceneColor:
    case RenderMaterialGraphNodeKind::SceneTexture:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::VertexColor:
    case RenderMaterialGraphNodeKind::DynamicParameter:
    case RenderMaterialGraphNodeKind::CollectionParameter:
        if (node.kind == RenderMaterialGraphNodeKind::CollectionParameter) {
            if (outputPin == "scalar") {
                return "(" + baseRef + ").x";
            }
            if (outputPin == "xyz") {
                return "(" + baseRef + ").xyz";
            }
        }
        if (outputPin == "r" || outputPin == "g" || outputPin == "b" || outputPin == "a") {
            return "(" + baseRef + ")." + std::string{ outputPin };
        }
        return baseRef;
    case RenderMaterialGraphNodeKind::CustomCode:
        if (outputPin == "value") {
            return baseRef;
        }
        if (const auto it = cg.customOutputTemps.find(CustomCodeOutputTempKey(node.id, outputPin)); it != cg.customOutputTemps.end()) {
            return it->second;
        }
        AddShaderGenerationDiagnostic(cg.diagnostics, node, outputPin, "CustomCode output pin was not emitted.");
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node, outputPin, true));
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
        return DefaultExpressionForType(RenderMaterialGraphPinDataType(node, outputPin, true));
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
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return "collectionParam" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample:
        return "textureSample" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObject:
        return "textureObject" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        return "textureSampleCube" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObjectCube:
        return "textureObjectCube" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        return "textureSampleVolume" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
        return "textureObjectVolume" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return "textureSample2DArray" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return "textureObject2DArray" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ConstantBool:
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
    return IsTextureSampleNode(node.kind) || IsTextureObjectNode(node.kind)
        ? "baseColor"
        : std::string{};
}

[[nodiscard]] std::string EffectiveTextureRoleForNode(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node) {
    if (TextureNodeFeedsSurfaceNormal(graph, node)) {
        return "normal";
    }
    return EffectiveTextureRoleForNode(node);
}

[[nodiscard]] RenderMaterialTextureColorSpace EffectiveTextureColorSpaceForNode(
    const RenderMaterialGraphNode& node,
    std::string_view role) noexcept {
    if (node.parameter.expectedTextureColorSpace != RenderMaterialTextureColorSpace::Unknown) {
        return node.parameter.expectedTextureColorSpace;
    }
    if (IsTextureSampleNode(node.kind) || IsTextureObjectNode(node.kind)) {
        return ExpectedColorSpaceForTextureRole(role).value_or(RenderMaterialTextureColorSpace::Srgb);
    }
    return RenderMaterialTextureColorSpace::Unknown;
}

[[nodiscard]] RenderMaterialTextureColorSpace EffectiveTextureColorSpaceForNode(
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& node,
    std::string_view role) noexcept {
    if (TextureNodeFeedsSurfaceNormal(graph, node)) {
        return RenderMaterialTextureColorSpace::Linear;
    }
    return EffectiveTextureColorSpaceForNode(node, role);
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
    case RenderMaterialGraphNodeKind::ConstantBool:
    case RenderMaterialGraphNodeKind::TextureSample:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureSampleCube:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
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

[[nodiscard]] bool HasOutputLink(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    return std::any_of(graph.links.begin(), graph.links.end(), [nodeId, pin](const RenderMaterialGraphLink& link) {
        return link.fromNodeId == nodeId && link.fromPin == pin;
    });
}

[[nodiscard]] std::vector<const RenderMaterialGraphNode*> NamedRerouteDeclarations(
    const RenderMaterialGraphDocument& graph,
    std::string_view key) {
    std::vector<const RenderMaterialGraphNode*> declarations;
    if (key.empty()) {
        return declarations;
    }
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration &&
            NamedRerouteKey(node) == key) {
            declarations.push_back(&node);
        }
    }
    return declarations;
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
    const RenderMaterialGraphNode* node = FindRenderMaterialGraphNode(graph, nodeId);
    if (node != nullptr && node->kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration) {
        const std::string key = NamedRerouteKey(*node);
        for (const RenderMaterialGraphNode& usage : graph.nodes) {
            if (usage.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage &&
                NamedRerouteKey(usage) == key &&
                VisitGraphNodeForCycle(graph, usage.id, states)) {
                return true;
            }
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

struct GraphEndpoint {
    std::uint32_t nodeId = 0U;
    std::string pin;
};

struct FunctionEndpointSignature {
    std::string name;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Float4;
    std::uint32_t nodeId = 0U;
};

struct FunctionSignature {
    std::vector<FunctionEndpointSignature> inputs;
    std::vector<FunctionEndpointSignature> outputs;
};

[[nodiscard]] bool HasGraphDiagnosticError(std::span<const RenderMaterialGraphDiagnostic> diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

[[nodiscard]] bool IsDeferredSceneBindingNode(RenderMaterialGraphNodeKind kind) noexcept {
    return kind == RenderMaterialGraphNodeKind::SceneDepth ||
        kind == RenderMaterialGraphNodeKind::SceneColor ||
        kind == RenderMaterialGraphNodeKind::SceneTexture ||
        kind == RenderMaterialGraphNodeKind::DepthFade;
}

[[nodiscard]] RenderMaterialGraphNodeSupport RenderMaterialGraphNodeSupportForDocumentPath(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphNodeKind kind,
    RenderMaterialGraphRenderPath path) noexcept {
    const RenderMaterialGraphNodeSupport pathSupport = RenderMaterialGraphNodeSupportForPath(kind, path);
    if (pathSupport == RenderMaterialGraphNodeSupport::Unsupported &&
        path == RenderMaterialGraphRenderPath::GpuDeferred &&
        IsDeferredSceneBindingNode(kind) &&
        IsRenderMaterialGraphBlendModeTransparent(ParseRenderMaterialGraphBlendMode(graph.blendMode))) {
        return RenderMaterialGraphNodeSupportStatus(kind);
    }
    return pathSupport;
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

std::vector<std::string> RenderMaterialGraphNodeInputPinNames(const RenderMaterialGraphNode& node) {
    RenderMaterialGraphIrNode irNode{ .nodeId = node.id, .kind = node.kind };
    AppendIrPins(irNode, node);
    std::vector<std::string> names;
    names.reserve(irNode.inputs.size());
    for (const RenderMaterialGraphIrPin& pin : irNode.inputs) {
        names.push_back(pin.name);
    }
    return names;
}

std::vector<std::string> RenderMaterialGraphNodeOutputPinNames(const RenderMaterialGraphNode& node) {
    RenderMaterialGraphIrNode irNode{ .nodeId = node.id, .kind = node.kind };
    AppendIrPins(irNode, node);
    std::vector<std::string> names;
    names.reserve(irNode.outputs.size());
    for (const RenderMaterialGraphIrPin& pin : irNode.outputs) {
        names.push_back(pin.name);
    }
    return names;
}

RenderMaterialGraphCompileArtifactCacheKey BuildRenderMaterialGraphCompileArtifactCacheKey(
    const RenderMaterialGraphDocument& sourceGraph,
    std::span<const RenderMaterialGraphDependencyHashInput> dependencies,
    std::uint64_t shaderIncludeHash,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphFunctionInlineResult inlineResult = InlineRenderMaterialGraphFunctions(sourceGraph, context);
    const RenderMaterialGraphDocument& graph = inlineResult.Succeeded() ? inlineResult.graph : sourceGraph;
    const std::uint64_t graphHash = RenderMaterialGraphShaderSemanticHash(graph);

    std::vector<RenderMaterialGraphDependencyHashInput> sortedDependencies(dependencies.begin(), dependencies.end());
    if (context.functionLibrary != nullptr) {
        std::vector<std::uint64_t> pending = DiscoverRenderMaterialGraphFunctionDependencies(sourceGraph);
        std::vector<std::uint64_t> visited;
        while (!pending.empty()) {
            const std::uint64_t assetId = pending.back();
            pending.pop_back();
            if (std::find(visited.begin(), visited.end(), assetId) != visited.end()) {
                continue;
            }
            visited.push_back(assetId);
            if (const RenderMaterialGraphFunctionLibraryEntry* entry = context.functionLibrary->Find(assetId)) {
                sortedDependencies.push_back(RenderMaterialGraphDependencyHashInput{
                    .assetId = entry->assetId,
                    .contentHash = entry->contentHash,
                    .name = entry->name,
                });
                for (const std::uint64_t nestedAssetId : DiscoverRenderMaterialGraphFunctionDependencies(entry->graph)) {
                    if (std::find(visited.begin(), visited.end(), nestedAssetId) == visited.end()) {
                        pending.push_back(nestedAssetId);
                    }
                }
            }
        }
    }
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
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::QualitySwitch)) {
        HashString64(combined, std::string{ RenderMaterialGraphQualityLevelPinName(context.qualityLevel) });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::FeatureLevelSwitch)) {
        HashString64(combined, std::string{ RenderMaterialGraphFeatureLevelPinName(context.featureLevel) });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::ShadingPathSwitch)) {
        HashString64(combined, std::string{ RenderMaterialGraphShadingPathPinName(context.shadingPath) });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::ShaderStageSwitch)) {
        HashString64(combined, std::string{ RenderMaterialGraphShaderStagePinName(context.shaderStage) });
    }
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
    result.key = BuildRenderMaterialGraphCompileArtifactCacheKey(graph, dependencies, shaderIncludeHash, context);
    if (const RenderMaterialGraphCompileArtifact* cached = cache.Find(result.key)) {
        result.compile.shader = cached->shader;
        RenderMaterialGraphFunctionInlineResult inlineResult = InlineRenderMaterialGraphFunctions(graph, context);
        result.compile.diagnostics = std::move(inlineResult.diagnostics);
        if (inlineResult.Succeeded()) {
            const RenderMaterialGraphRenderPath renderPath = context.shadingPath == RenderMaterialGraphShadingPath::Deferred
                ? RenderMaterialGraphRenderPath::GpuDeferred
                : RenderMaterialGraphRenderPath::GpuForward;
            std::vector<RenderMaterialGraphDiagnostic> validationDiagnostics = ValidateRenderMaterialGraphDocument(inlineResult.graph, renderPath);
            AttachDiagnosticContext(inlineResult.graph, context, validationDiagnostics);
            result.compile.diagnostics.insert(
                result.compile.diagnostics.end(),
                std::make_move_iterator(validationDiagnostics.begin()),
                std::make_move_iterator(validationDiagnostics.end()));
        } else {
            AttachDiagnosticContext(graph, context, result.compile.diagnostics);
        }
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
    const RenderMaterialGraphDocument& sourceGraph,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphIrBuildResult result{};
    RenderMaterialGraphFunctionInlineResult inlineResult = InlineRenderMaterialGraphFunctions(sourceGraph, context);
    result.diagnostics = std::move(inlineResult.diagnostics);
    if (!inlineResult.Succeeded()) {
        AttachDiagnosticContext(sourceGraph, context, result.diagnostics);
        return result;
    }
    const RenderMaterialGraphDocument graph = std::move(inlineResult.graph);

    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (!IsKnownNodeKind(node.kind)) {
            continue;
        }
        RenderMaterialGraphIrNode irNode{
            .nodeId = node.id,
            .kind = node.kind,
        };
        AppendIrPins(irNode, node);
        result.ir.nodes.push_back(std::move(irNode));
    }

    for (const RenderMaterialGraphLink& link : graph.links) {
        const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
        if (fromNode == nullptr || toNode == nullptr || !IsKnownNodeKind(fromNode->kind) || !IsKnownNodeKind(toNode->kind)) {
            continue;
        }

        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(*fromNode, link.fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(*toNode, link.toPin, false);
        const std::uint32_t fromPinId = link.fromPinId != 0U
            ? link.fromPinId
            : RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
        const std::uint32_t toPinId = link.toPinId != 0U
            ? link.toPinId
            : RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
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

    const RenderMaterialGraphRenderPath renderPath = context.shadingPath == RenderMaterialGraphShadingPath::Deferred
        ? RenderMaterialGraphRenderPath::GpuDeferred
        : RenderMaterialGraphRenderPath::GpuForward;
    std::vector<RenderMaterialGraphDiagnostic> validationDiagnostics = ValidateRenderMaterialGraphDocument(graph, renderPath);
    AttachDiagnosticContext(graph, context, validationDiagnostics);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(validationDiagnostics.begin()),
        std::make_move_iterator(validationDiagnostics.end()));
    return result;
}

RenderMaterialGraphCompileResult CompileRenderMaterialGraphToShaderSource(
    const RenderMaterialGraphDocument& sourceGraph,
    RenderMaterialGraphBuildContext context) {
    g_renderMaterialGraphCompileInvocationCount.fetch_add(1U, std::memory_order_relaxed);
    {
        std::ostringstream row;
        row << "compile-start asset=" << context.assetId
            << " sourcePath=" << context.sourcePath
            << " nodes=" << sourceGraph.nodes.size()
            << " links=" << sourceGraph.links.size()
            << " stage=" << static_cast<int>(context.shaderStage);
        WriteRendererMaterialGraphDebugLog("compile", row.str());
    }
    RenderMaterialGraphCompileResult result{};
    RenderMaterialGraphFunctionInlineResult inlineResult = InlineRenderMaterialGraphFunctions(sourceGraph, context);
    result.diagnostics = std::move(inlineResult.diagnostics);
    if (!inlineResult.Succeeded()) {
        AttachDiagnosticContext(sourceGraph, context, result.diagnostics);
        WriteRendererMaterialGraphDebugLog("compile", "compile-failed inline-functions diagnostics=" + std::to_string(result.diagnostics.size()));
        return result;
    }

    const RenderMaterialGraphDocument graph = std::move(inlineResult.graph);
    RenderMaterialGraphBuildContext inlinedContext = context;
    inlinedContext.functionLibrary = nullptr;
    RenderMaterialGraphIrBuildResult ir = BuildRenderMaterialGraphIr(graph, inlinedContext);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(ir.diagnostics.begin()),
        std::make_move_iterator(ir.diagnostics.end()));
    if (!ir.Succeeded()) {
        WriteRendererMaterialGraphDebugLog("compile", "compile-failed ir diagnostics=" + std::to_string(result.diagnostics.size()));
        return result;
    }

    // MAT-34: only the Surface domain has a production graph pipeline. Declared domains without a runtime
    // pass fail compilation instead of silently compiling as Surface.
    const RenderMaterialDomain requestedDomain = ParseRenderMaterialDomain(graph.materialDomain);
    if (!IsRenderMaterialDomainProduction(requestedDomain)) {
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Error,
            .kind = RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain,
            .message = "Material domain '" + std::string{ RenderMaterialDomainName(requestedDomain) } +
                "' is declared but has no production graph runtime pass.",
        });
        AttachDiagnosticContext(graph, context, result.diagnostics);
        return result;
    }

    // MAT-37: resolve the surface shading model. Production models have real fragment-wrapper branches;
    // declared-but-unimplemented models fail here so the runtime never silently shades them as DefaultLit.
    const RenderMaterialShadingModel requestedShadingModel = ParseRenderMaterialShadingModel(graph.shadingModel);
    if (!IsRenderMaterialShadingModelProduction(requestedShadingModel)) {
        result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Error,
            .kind = RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel,
            .message = "Shading model '" + std::string{ RenderMaterialShadingModelName(requestedShadingModel) } +
                "' is declared but has no production graph runtime branch.",
        });
        AttachDiagnosticContext(graph, context, result.diagnostics);
        return result;
    }
    const RenderMaterialShadingModel resolvedShadingModel = requestedShadingModel;

    // MAT-38: resolve the blend mode. All seven modes are implemented, so there is no fallback; the value
    // selects the masked clip in the wrapper and the transparent cook/scene blend equation downstream.
    const RenderMaterialGraphBlendMode resolvedBlendMode = ParseRenderMaterialGraphBlendMode(graph.blendMode);

    const RenderMaterialGraphNode* outputNode = nullptr;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            outputNode = &node;
            break;
        }
    }
    if (outputNode == nullptr) {
        WriteRendererMaterialGraphDebugLog("compile", "compile-failed no MaterialOutput node");
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
        RenderMaterialGraphReflectionUniformSource source = RenderMaterialGraphReflectionUniformSource::MaterialParameter;
        std::uint64_t collectionAssetId = 0U;
        std::string collectionParameterStableId;
        std::array<float, 4U> defaultValue{};
    };
    struct ReflectionTextureEntry {
        std::string samplerName;
        std::string stableId;
        std::string role;
        RenderMaterialTextureColorSpace colorSpace;
        RenderMaterialGraphSamplerState samplerState;
        RenderMaterialGraphTextureDimension dimension;
    };

    std::vector<ReflectionUniformEntry> uniformEntries;
    std::vector<ReflectionTextureEntry> textureEntries;
    bool needsUv0 = false;
    bool usesSceneDepth = false;
    bool usesSceneColor = false;
    bool usesHsv = false;
    bool usesFastTrig = false;
    bool usesRuntimeSwitch = false;
    bool usesBlackBody = false;
    bool usesNoise = false;
    bool usesSobol = false;
    bool usesVertexColor = false;
    bool usesPerInstanceRandom = false;
    bool usesPerInstanceFadeAmount = false;
    bool usesPerInstanceCustomData = false;
    bool usesPreSkinnedPosition = false;
    bool usesPreSkinnedNormal = false;

    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (std::find(reachable.begin(), reachable.end(), node.id) == reachable.end()) {
            continue;
        }
        switch (node.kind) {
        case RenderMaterialGraphNodeKind::SceneDepth:
        case RenderMaterialGraphNodeKind::DepthFade:
            // MAT-80/#18b: these nodes sample the opaque scene depth (screen-space) which the scene binds
            // into the graph fragment shader for the transparent pass.
            usesSceneDepth = true;
            break;
        case RenderMaterialGraphNodeKind::SceneColor:
            usesSceneColor = true;
            break;
        case RenderMaterialGraphNodeKind::SceneTexture:
            if (SceneTextureReadsDepth(node)) {
                usesSceneDepth = true;
            } else {
                usesSceneColor = true;
            }
            break;
        case RenderMaterialGraphNodeKind::HsvToRgb:
        case RenderMaterialGraphNodeKind::RgbToHsv:
            // MAT-50: the HSV<->RGB conversions call shared helper functions emitted into the shader prelude.
            usesHsv = true;
            break;
        case RenderMaterialGraphNodeKind::ArcSineFast:
        case RenderMaterialGraphNodeKind::ArcCosineFast:
        case RenderMaterialGraphNodeKind::ArcTangentFast:
        case RenderMaterialGraphNodeKind::ArcTangent2Fast:
            usesFastTrig = true;
            break;
        case RenderMaterialGraphNodeKind::RuntimeSwitch:
            usesRuntimeSwitch = true;
            break;
        case RenderMaterialGraphNodeKind::BlackBody:
            // MAT-50: BlackBody calls a shared Planckian-locus helper emitted into the shader prelude.
            usesBlackBody = true;
            break;
        case RenderMaterialGraphNodeKind::Noise:
        case RenderMaterialGraphNodeKind::VectorNoise:
            // MAT-50: the noise nodes share hash + value-noise helpers emitted into the shader prelude.
            usesNoise = true;
            break;
        case RenderMaterialGraphNodeKind::Sobol:
            usesSobol = true;
            break;
        case RenderMaterialGraphNodeKind::ParameterScalar:
            uniformEntries.push_back({ ParameterUniformName(node, ""), StableParameterId(node), node.kind,
                RenderMaterialGraphReflectionUniformSource::MaterialParameter, 0U, {}, DynamicParameterDefaultValue(node) });
            break;
        case RenderMaterialGraphNodeKind::ParameterVector:
            uniformEntries.push_back({ ParameterUniformName(node, "_xyz"), StableParameterId(node), node.kind,
                RenderMaterialGraphReflectionUniformSource::MaterialParameter, 0U, {}, DynamicParameterDefaultValue(node) });
            break;
        case RenderMaterialGraphNodeKind::ParameterColor:
            uniformEntries.push_back({ ParameterUniformName(node, "_rgba"), StableParameterId(node), node.kind,
                RenderMaterialGraphReflectionUniformSource::MaterialParameter, 0U, {}, DynamicParameterDefaultValue(node) });
            break;
        case RenderMaterialGraphNodeKind::CollectionParameter:
            uniformEntries.push_back(ReflectionUniformEntry{
                .name = CollectionParameterUniformName(node),
                .stableId = StableParameterId(node),
                .kind = node.kind,
                .source = RenderMaterialGraphReflectionUniformSource::ParameterCollection,
                .collectionAssetId = RenderMaterialGraphCollectionAssetId(node),
                .collectionParameterStableId = StableParameterId(node),
            });
            break;
        case RenderMaterialGraphNodeKind::TextureSample:
        case RenderMaterialGraphNodeKind::TextureSampleVolume:
        case RenderMaterialGraphNodeKind::TextureSample2DArray:
            if (!HasInputLink(graph, node.id, "texture")) {
                const std::string textureRole = EffectiveTextureRoleForNode(graph, node);
                textureEntries.push_back({
                    ParameterUniformName(node, "_texture"),
                    StableParameterId(node),
                    textureRole,
                    EffectiveTextureColorSpaceForNode(graph, node, textureRole),
                    node.parameter.samplerState,
                    TextureDimensionForNode(node.kind),
                });
            }
            needsUv0 = true;
            break;
        case RenderMaterialGraphNodeKind::TextureSampleCube:
            if (!HasInputLink(graph, node.id, "texture")) {
                const std::string textureRole = EffectiveTextureRoleForNode(graph, node);
                textureEntries.push_back({
                    ParameterUniformName(node, "_texture"),
                    StableParameterId(node),
                    textureRole,
                    EffectiveTextureColorSpaceForNode(graph, node, textureRole),
                    node.parameter.samplerState,
                    TextureDimensionForNode(node.kind),
                });
            }
            break;
        case RenderMaterialGraphNodeKind::ParameterTexture:
        case RenderMaterialGraphNodeKind::TextureObject:
        case RenderMaterialGraphNodeKind::TextureObjectCube:
        case RenderMaterialGraphNodeKind::TextureObjectVolume:
        case RenderMaterialGraphNodeKind::TextureObject2DArray: {
            const std::string textureRole = EffectiveTextureRoleForNode(graph, node);
            textureEntries.push_back({
                ParameterUniformName(node, "_texture"),
                StableParameterId(node),
                textureRole,
                EffectiveTextureColorSpaceForNode(graph, node, textureRole),
                node.parameter.samplerState,
                TextureDimensionForNode(node.kind),
            });
            break;
        }
        case RenderMaterialGraphNodeKind::VertexColor:
            usesVertexColor = true;
            break;
        case RenderMaterialGraphNodeKind::PerInstanceRandom:
            usesPerInstanceRandom = true;
            break;
        case RenderMaterialGraphNodeKind::PerInstanceFadeAmount:
        case RenderMaterialGraphNodeKind::DistanceCullFade:
            usesPerInstanceFadeAmount = true;
            break;
        case RenderMaterialGraphNodeKind::PerInstanceCustomData:
            usesPerInstanceCustomData = true;
            break;
        case RenderMaterialGraphNodeKind::PreSkinnedPosition:
            usesPreSkinnedPosition = true;
            break;
        case RenderMaterialGraphNodeKind::PreSkinnedNormal:
            usesPreSkinnedNormal = true;
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
        if (a.source != b.source) {
            return static_cast<std::uint8_t>(a.source) < static_cast<std::uint8_t>(b.source);
        }
        if (a.collectionAssetId != b.collectionAssetId) {
            return a.collectionAssetId < b.collectionAssetId;
        }
        if (a.stableId != b.stableId) {
            return a.stableId < b.stableId;
        }
        return a.name < b.name;
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
        source += std::string{ TextureSamplerMacro(textureEntries[index].dimension) } + "(" +
            textureEntries[index].samplerName + ", " + std::to_string(stage) + ");\n";
    }
    if (usesSceneColor) {
        // MAT-31: transparent graph materials can sample an opaque scene-color snapshot bound at slot 4.
        source += "SAMPLER2D(s_kbSceneColor, 4);\n";
    }
    if (usesSceneDepth) {
        // MAT-80/#18b: the opaque scene depth is bound by the scene at the reserved slot 5 (a graph fragment
        // shader does not use the builtin PBR sampler slots 0-5; 6+ are the graph's own textures).
        source += "SAMPLER2D(s_kbSceneDepth, 5);\n";
    }
    if (!uniformEntries.empty() || !textureEntries.empty() || usesSceneColor || usesSceneDepth) {
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
    source += "    float deltaTime;\n";
    source += "    vec4 dynamicParameter;\n";
    source += "    vec2 screenPosition;\n";
    source += "    vec3 localPosition;\n";
    source += "    vec3 objectPosition;\n";
    source += "    float perInstanceRandom;\n";
    source += "    float perInstanceFadeAmount;\n";
    source += "    float perInstanceCustomData;\n";
    source += "    float objectRadius;\n";
    source += "    vec4 objectBounds;\n";
    source += "    vec3 objectOrientation;\n";
    source += "    vec3 preSkinnedPosition;\n";
    source += "    vec3 preSkinnedNormal;\n";
    // MAT-46: world/object-space inputs populated by the wrapper in every pass so the nodes are shadow-safe.
    source += "    vec3 cameraPosition;\n";
    source += "    vec3 lightVector;\n";
    source += "    vec2 viewSize;\n";
    source += "    float twoSidedSign;\n";
    // MAT-80/#18b: this fragment's device depth (gl_FragCoord.z), so DepthFade can compare it against the
    // sampled opaque scene depth for a soft edge where translucency meets solid geometry.
    source += "    float fragmentDepth;\n";
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
    source += "    vec3 tangentOutput;\n";
    source += "};\n\n";

    if (usesHsv) {
        // MAT-50: branchless HSV<->RGB helpers (Sam Hocevar's formulation) shared by the HsvToRgb/RgbToHsv nodes.
        source += "vec3 kbHsvToRgb(vec3 c) {\n";
        source += "    vec4 K = vec4(1.0, 0.666666, 0.333333, 3.0);\n";
        source += "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n";
        source += "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n";
        source += "}\n\n";
        source += "vec3 kbRgbToHsv(vec3 c) {\n";
        source += "    vec4 K = vec4(0.0, -0.333333, 0.666666, -1.0);\n";
        source += "    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n";
        source += "    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n";
        source += "    float d = q.x - min(q.w, q.y);\n";
        source += "    float e = 1.0e-10;\n";
        source += "    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);\n";
        source += "}\n\n";
    }

    if (usesFastTrig) {
        source += "vec4 kbAtanFast(vec4 x) {\n";
        source += "    vec4 ax = abs(x);\n";
        source += "    vec4 t = mix(ax, vec4_splat(1.0) / max(ax, vec4_splat(0.000001)), step(vec4_splat(1.0), ax));\n";
        source += "    vec4 s = t * t;\n";
        source += "    vec4 r = (((vec4_splat(-0.0464964749) * s + vec4_splat(0.15931422)) * s - vec4_splat(0.327622764)) * s + vec4_splat(0.999787841)) * t;\n";
        source += "    r = mix(r, vec4_splat(1.57079632679) - r, step(vec4_splat(1.0), ax));\n";
        source += "    return r * sign(x);\n";
        source += "}\n\n";
        source += "vec4 kbAtan2Fast(vec4 y, vec4 x) {\n";
        source += "    vec4 ax = abs(x);\n";
        source += "    vec4 nearZero = step(ax, vec4_splat(0.000001));\n";
        source += "    vec4 safeX = mix(x, vec4_splat(0.000001), nearZero);\n";
        source += "    vec4 angle = kbAtanFast(y / safeX);\n";
        source += "    vec4 piOffset = mix(vec4_splat(-3.14159265359), vec4_splat(3.14159265359), step(vec4_splat(0.0), y));\n";
        source += "    angle += (vec4_splat(1.0) - step(vec4_splat(0.0), x)) * piOffset;\n";
        source += "    vec4 vertical = mix(vec4_splat(-1.57079632679), vec4_splat(1.57079632679), step(vec4_splat(0.0), y));\n";
        source += "    return mix(angle, vertical, nearZero);\n";
        source += "}\n\n";
        source += "vec4 kbAsinFast(vec4 x) {\n";
        source += "    vec4 c = clamp(x, vec4_splat(-1.0), vec4_splat(1.0));\n";
        source += "    return kbAtan2Fast(c, sqrt(max(vec4_splat(0.0), vec4_splat(1.0) - c * c)));\n";
        source += "}\n\n";
        source += "vec4 kbAcosFast(vec4 x) {\n";
        source += "    return vec4_splat(1.57079632679) - kbAsinFast(x);\n";
        source += "}\n\n";
    }

    if (usesRuntimeSwitch) {
        source += "vec4 kbSwitch4(float indexValue, vec4 defaultValue, vec4 case0Value, vec4 case1Value, vec4 case2Value, vec4 case3Value) {\n";
        source += "    float selected = floor(indexValue + 0.5);\n";
        source += "    float case0Mask = 1.0 - step(0.5, abs(selected - 0.0));\n";
        source += "    float case1Mask = 1.0 - step(0.5, abs(selected - 1.0));\n";
        source += "    float case2Mask = 1.0 - step(0.5, abs(selected - 2.0));\n";
        source += "    float case3Mask = 1.0 - step(0.5, abs(selected - 3.0));\n";
        source += "    float selectedMask = max(max(case0Mask, case1Mask), max(case2Mask, case3Mask));\n";
        source += "    return defaultValue * (1.0 - selectedMask) + case0Value * case0Mask + case1Value * case1Mask + case2Value * case2Mask + case3Value * case3Mask;\n";
        source += "}\n\n";
    }

    if (usesBlackBody) {
        // MAT-50: Tanner Helland's blackbody approximation, normalized to [0,1]; bases are guarded so the
        // unselected ternary branch can never feed pow()/log() a negative argument.
        source += "vec3 kbBlackBody(float kelvin) {\n";
        source += "    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;\n";
        source += "    float r = t <= 66.0 ? 1.0 : clamp(1.29293618606 * pow(max(t - 60.0, 0.0001), -0.1332047592), 0.0, 1.0);\n";
        source += "    float g = t <= 66.0\n";
        source += "        ? clamp(0.39008157876 * log(max(t, 0.0001)) - 0.63184144378, 0.0, 1.0)\n";
        source += "        : clamp(1.12989086089 * pow(max(t - 60.0, 0.0001), -0.0755148492), 0.0, 1.0);\n";
        source += "    float b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : clamp(0.54320678911 * log(max(t - 10.0, 0.0001)) - 1.19625408914, 0.0, 1.0));\n";
        source += "    return vec3(r, g, b);\n";
        source += "}\n\n";
    }

    if (usesNoise) {
        // MAT-50: integer-lattice hash + trilinearly interpolated value noise (deterministic, no textures).
        source += "float kbHash(vec3 p) {\n";
        source += "    p = fract(p * 0.3183099 + vec3(0.1, 0.1, 0.1));\n";
        source += "    p *= 17.0;\n";
        source += "    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n";
        source += "}\n\n";
        source += "float kbValueNoise(vec3 x) {\n";
        source += "    vec3 i = floor(x);\n";
        source += "    vec3 f = fract(x);\n";
        source += "    f = f * f * (vec3(3.0, 3.0, 3.0) - 2.0 * f);\n";
        source += "    float n000 = kbHash(i + vec3(0.0, 0.0, 0.0));\n";
        source += "    float n100 = kbHash(i + vec3(1.0, 0.0, 0.0));\n";
        source += "    float n010 = kbHash(i + vec3(0.0, 1.0, 0.0));\n";
        source += "    float n110 = kbHash(i + vec3(1.0, 1.0, 0.0));\n";
        source += "    float n001 = kbHash(i + vec3(0.0, 0.0, 1.0));\n";
        source += "    float n101 = kbHash(i + vec3(1.0, 0.0, 1.0));\n";
        source += "    float n011 = kbHash(i + vec3(0.0, 1.0, 1.0));\n";
        source += "    float n111 = kbHash(i + vec3(1.0, 1.0, 1.0));\n";
        source += "    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),\n";
        source += "               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);\n";
        source += "}\n\n";
        source += "vec3 kbVectorNoise(vec3 x) {\n";
        source += "    return vec3(kbValueNoise(x), kbValueNoise(x + vec3(31.416, 47.853, 12.793)), kbValueNoise(x + vec3(57.719, 93.981, 74.321)));\n";
        source += "}\n\n";
    }

    if (usesSobol) {
        // MAT-50 tier2: deterministic 2D Sobol sample without UE's SobolSamplingTexture dependency.
        source += "float kbXor16(float a, float b) {\n";
        source += "    float result = 0.0;\n";
        source += "    float bitValue = 1.0;\n";
        source += "    for (int bitIndex = 0; bitIndex < 16; ++bitIndex) {\n";
        source += "        float abit = mod(floor(a / bitValue), 2.0);\n";
        source += "        float bbit = mod(floor(b / bitValue), 2.0);\n";
        source += "        result += mod(abit + bbit, 2.0) * bitValue;\n";
        source += "        bitValue *= 2.0;\n";
        source += "    }\n";
        source += "    return result;\n";
        source += "}\n\n";
        source += "vec2 kbSobolApply(vec2 result, float indexValue, float bitValue, vec2 direction) {\n";
        source += "    float bitSet = mod(floor(indexValue / bitValue), 2.0);\n";
        source += "    vec2 xored = vec2(kbXor16(result.x, direction.x), kbXor16(result.y, direction.y));\n";
        source += "    return mix(result, xored, vec2_splat(bitSet));\n";
        source += "}\n\n";
        source += "vec2 kbSobol2(vec2 cell, float indexValue, vec2 seed) {\n";
        source += "    vec2 origin = floor(cell);\n";
        source += "    vec2 c = floor(abs(cell));\n";
        source += "    vec2 result = mod(floor(vec2(c.x * 1973.0 + c.y * 9277.0, c.x * 26699.0 + c.y * 31847.0)), vec2_splat(65536.0));\n";
        source += "    float idx = floor(max(indexValue, 0.0));\n";
        source += "    result = kbSobolApply(result, idx, 1.0, vec2(34432.0, 19584.0));\n";
        source += "    result = kbSobolApply(result, idx, 2.0, vec2(62016.0, 37440.0));\n";
        source += "    result = kbSobolApply(result, idx, 4.0, vec2(33312.0, 3616.0));\n";
        source += "    result = kbSobolApply(result, idx, 8.0, vec2(16656.0, 5648.0));\n";
        source += "    result = kbSobolApply(result, idx, 16.0, vec2(42504.0, 30216.0));\n";
        source += "    result = kbSobolApply(result, idx, 32.0, vec2(35330.0, 10250.0));\n";
        source += "    result = kbSobolApply(result, idx, 64.0, vec2(57860.0, 40452.0));\n";
        source += "    result = kbSobolApply(result, idx, 128.0, vec2(41984.0, 18050.0));\n";
        source += "    result = kbSobolApply(result, idx, 256.0, vec2(58112.0, 42829.0));\n";
        source += "    result = kbSobolApply(result, idx, 512.0, vec2(46848.0, 38935.0));\n";
        source += "    vec2 seedBits = floor(fract(abs(seed)) * 65536.0);\n";
        source += "    result = vec2(kbXor16(result.x, seedBits.x), kbXor16(result.y, seedBits.y));\n";
        source += "    return origin + (result / 65536.0);\n";
        source += "}\n\n";
    }

    RenderMaterialGraphBuildContext fragmentContext = context;
    fragmentContext.shaderStage = RenderMaterialGraphShaderStage::Fragment;
    GraphCodegen cg{ .graph = graph, .context = fragmentContext, .diagnostics = result.diagnostics };
    for (const RenderMaterialGraphLink& link : graph.links) {
        ++cg.fanOut[link.fromNodeId];
    }
    const auto compileOutput = [&cg, outputNode](std::string_view outputPin, RenderMaterialGraphPinType outputType, std::string fallback) {
        return CompileInputExpression(cg, *outputNode, outputPin, outputType, std::move(fallback));
    };

    const bool useMaterialAttributes = HasInputLink(graph, outputNode->id, "attributes");
    std::string attributesExpr;
    std::string baseColorExpr;
    std::string metallicExpr;
    std::string roughnessExpr;
    std::string normalExpr;
    std::string occlusionExpr;
    std::string emissiveExpr;
    std::string alphaExpr;
    std::string alphaClipThresholdExpr;
    std::string specularExpr;
    std::string tangentOutputExpr;

    // MAT-36: when a single MaterialAttributes set is wired into MaterialOutput.attributes it drives the
    // whole surface (UE's "Use Material Attributes" mode); the per-channel pins are bypassed entirely.
    if (useMaterialAttributes) {
        attributesExpr = compileOutput("attributes", RenderMaterialGraphPinType::MaterialAttributes, "");
    } else {
        baseColorExpr = compileOutput("baseColor", RenderMaterialGraphPinType::Color, "vec4(1.0, 1.0, 1.0, 1.0)");
        metallicExpr = compileOutput("metallic", RenderMaterialGraphPinType::Float, "0.0");
        roughnessExpr = compileOutput("roughness", RenderMaterialGraphPinType::Float, "1.0");
        normalExpr = compileOutput("normal", RenderMaterialGraphPinType::Normal, "vec3(0.0, 0.0, 1.0)");
        occlusionExpr = compileOutput("occlusion", RenderMaterialGraphPinType::Float, "1.0");
        emissiveExpr = compileOutput("emissive", RenderMaterialGraphPinType::Color, "vec4(0.0, 0.0, 0.0, 1.0)");
        alphaExpr = compileOutput("alpha", RenderMaterialGraphPinType::Float, "1.0");
        alphaClipThresholdExpr = compileOutput("alphaClipThreshold", RenderMaterialGraphPinType::Float, "0.5");
        specularExpr = compileOutput("specular", RenderMaterialGraphPinType::Float, "0.5");
        tangentOutputExpr = compileOutput("tangentOutput", RenderMaterialGraphPinType::Float3, "vec3(1.0, 0.0, 0.0)");
    }
    {
        std::ostringstream row;
        row << "compile-surface-expr useAttributes=" << (useMaterialAttributes ? "true" : "false")
            << " outputNode=" << outputNode->id
            << " normalLinked=" << (HasInputLink(graph, outputNode->id, "normal") ? "true" : "false")
            << " normalExpr=" << (normalExpr.empty() ? "<attributes-or-empty>" : normalExpr);
        WriteRendererMaterialGraphDebugLog("compile", row.str());
    }

    std::unordered_set<std::uint32_t> emittedCustomFunctionDefinitions;
    AppendCustomCodeFunctionDefinitions(source, cg.functionDefinitions, emittedCustomFunctionDefinitions);

    source += "MaterialSurface EvaluateMaterialGraph(MaterialGraphContext ctx) {\n";
    source += "    MaterialSurface material;\n";
    if (useMaterialAttributes) {
        source += cg.statements;
        source += "    material = " + attributesExpr + ";\n";
    } else {
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
        source += "    material.tangentOutput = " + tangentOutputExpr + ";\n";
    }
    source += "    return material;\n";
    source += "}\n";

    // MAT-67/#54: vertex-domain outputs compile with fresh codegen so each generated function only
    // contains its own subgraph. The generated vertex shader evaluates them with a vertex-populated
    // context and writes real geometry/UV state before rasterization.
    const auto appendVertexOutputFunction = [&](
        std::string_view inputPin,
        RenderMaterialGraphPinType type,
        std::string_view defaultExpr,
        std::string_view returnType,
        std::string_view functionName) {
        RenderMaterialGraphBuildContext vertexContext = context;
        vertexContext.shaderStage = RenderMaterialGraphShaderStage::Vertex;
        GraphCodegen vertexCg{ .graph = graph, .context = vertexContext, .diagnostics = result.diagnostics };
        for (const RenderMaterialGraphLink& link : graph.links) {
            ++vertexCg.fanOut[link.fromNodeId];
        }
        const std::string expr = CompileInputExpression(vertexCg, *outputNode, inputPin, type, std::string{ defaultExpr });
        AppendCustomCodeFunctionDefinitions(source, vertexCg.functionDefinitions, emittedCustomFunctionDefinitions);
        source += "\n";
        source += std::string{ returnType } + " " + std::string{ functionName } + "(MaterialGraphContext ctx) {\n";
        source += vertexCg.statements;
        source += "    return " + expr + ";\n";
        source += "}\n";
    };
    const bool hasWorldPositionOffset = HasInputLink(graph, outputNode->id, "worldPositionOffset");
    if (hasWorldPositionOffset) {
        appendVertexOutputFunction("worldPositionOffset", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", "vec3", "EvaluateWorldPositionOffset");
    }
    const bool hasCustomizedUv0 = HasInputLink(graph, outputNode->id, "customizedUv0");
    if (hasCustomizedUv0) {
        appendVertexOutputFunction("customizedUv0", RenderMaterialGraphPinType::Float2, "ctx.uv0", "vec2", "EvaluateCustomizedUv0");
    }
    const bool hasDisplacement = HasInputLink(graph, outputNode->id, "displacement");
    if (hasDisplacement) {
        appendVertexOutputFunction("displacement", RenderMaterialGraphPinType::Float3, "vec3(0.0, 0.0, 0.0)", "vec3", "EvaluateDisplacement");
    }
    const bool hasTangentOutput = useMaterialAttributes || HasInputLink(graph, outputNode->id, "tangentOutput");

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
            .source = u.source,
            .collectionAssetId = u.collectionAssetId,
            .collectionParameterStableId = u.collectionParameterStableId,
            .defaultValue = u.defaultValue,
        });
    }
    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(textureEntries.size()); ++index) {
        reflection.textures.push_back(RenderMaterialGraphReflectionTexture{
            .samplerName = textureEntries[index].samplerName,
            .stableId = textureEntries[index].stableId,
            .slot = kRenderMaterialGraphTextureBaseSlot + index,
            .role = textureEntries[index].role,
            .colorSpace = textureEntries[index].colorSpace,
            .samplerState = textureEntries[index].samplerState,
            .dimension = textureEntries[index].dimension,
        });
    }
    if (needsUv0) {
        reflection.requiredVaryings.push_back("uv0");
    }
    if (usesVertexColor) {
        reflection.requiredVaryings.push_back("vertexColor");
    }
    if (usesPerInstanceRandom) {
        reflection.requiredVaryings.push_back("perInstanceRandom");
    }
    if (usesPerInstanceFadeAmount) {
        reflection.requiredVaryings.push_back("perInstanceFadeAmount");
    }
    if (usesPerInstanceCustomData) {
        reflection.requiredVaryings.push_back("perInstanceCustomData0");
    }
    if (usesPreSkinnedPosition) {
        reflection.requiredVaryings.push_back("preSkinnedPosition");
    }
    if (usesPreSkinnedNormal) {
        reflection.requiredVaryings.push_back("preSkinnedNormal");
    }
    reflection.hasWorldPositionOffset = hasWorldPositionOffset;
    reflection.hasCustomizedUv0 = hasCustomizedUv0;
    reflection.hasDisplacement = hasDisplacement;
    reflection.hasTangentOutput = hasTangentOutput;
    reflection.shadingModel = resolvedShadingModel;
    reflection.blendMode = resolvedBlendMode;
    reflection.usesSceneDepth = usesSceneDepth;
    reflection.usesSceneColor = usesSceneColor;

    result.shader = RenderMaterialGraphShaderSource{
        .entryPoint = "EvaluateMaterialGraph",
        .source = std::move(source),
        .sourceHash = hash,
        .reflection = std::move(reflection),
    };
    {
        std::ostringstream row;
        row << "compile-ok asset=" << context.assetId
            << " sourceHash=" << result.shader.sourceHash
            << " textures=" << result.shader.reflection.textures.size()
            << " uniforms=" << result.shader.reflection.uniforms.size()
            << " varyings=" << result.shader.reflection.requiredVaryings.size()
            << " hasTangentOutput=" << (result.shader.reflection.hasTangentOutput ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("compile", row.str());
    }
    for (const RenderMaterialGraphReflectionTexture& texture : result.shader.reflection.textures) {
        std::ostringstream row;
        row << "compile-texture sampler=" << texture.samplerName
            << " stableId=" << texture.stableId
            << " slot=" << texture.slot
            << " colorSpace=" << TextureColorSpaceName(texture.colorSpace);
        WriteRendererMaterialGraphDebugLog("compile", row.str());
    }
    return result;
}

std::uint64_t RenderMaterialGraphCompileInvocationCount() noexcept {
    return g_renderMaterialGraphCompileInvocationCount.load(std::memory_order_relaxed);
}

void ValidateCustomCodePin(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    const RenderMaterialGraphNode& node,
    const RenderMaterialGraphCustomPin& pin,
    std::string_view role,
    std::unordered_set<std::string>& names) {
    if (!IsShaderIdentifier(pin.name)) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            std::string{ role },
            "CustomCode " + std::string{ role } + " pin name '" + pin.name + "' is not a valid shader identifier.");
    }
    if (!names.insert(pin.name).second) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            pin.name,
            "CustomCode pin name '" + pin.name + "' is declared more than once.");
    }
    if (!IsCustomCodeValueType(pin.type)) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            pin.name,
            "CustomCode pin '" + pin.name + "' uses unsupported type '" + std::string{ RenderMaterialGraphPinTypeName(pin.type) } + "'.");
    }
}

void ValidateCustomCodeNode(
    const RenderMaterialGraphNode& node,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    if (node.customCode.body.empty()) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            "body",
            "CustomCode node requires a shader body.");
    }
    if (!IsCustomCodeValueType(node.customCode.outputType)) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            node.id,
            0U,
            "value",
            "CustomCode value output uses unsupported type '" + std::string{ RenderMaterialGraphPinTypeName(node.customCode.outputType) } + "'.");
    }

    std::unordered_set<std::string> names;
    names.insert("value");
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.inputs) {
        ValidateCustomCodePin(diagnostics, node, pin, "input", names);
    }
    for (const RenderMaterialGraphCustomPin& pin : node.customCode.outputs) {
        if (pin.name == "value") {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                node.id,
                0U,
                pin.name,
                "CustomCode additional output cannot be named 'value'.");
        }
        ValidateCustomCodePin(diagnostics, node, pin, "output", names);
    }
}

std::vector<RenderMaterialGraphDiagnostic> ValidateRenderMaterialGraphDocument(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphRenderPath renderPath) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
    const RenderMaterialGraphNode* outputNode = nullptr;
    std::size_t outputNodeCount = 0U;
    std::unordered_map<std::string, std::uint32_t> parameterStableIds;
    std::unordered_map<std::string, std::uint32_t> collectionParameterStableIds;
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
        if (node.parameter.hasRange &&
            (!std::isfinite(node.parameter.rangeMin) || !std::isfinite(node.parameter.rangeMax) ||
             node.parameter.rangeMin > node.parameter.rangeMax)) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                node.id,
                0U,
                "range",
                "Material graph numeric range must contain finite ordered bounds.");
        }
        if (!node.parameter.defaultValueHint.empty() && node.parameter.defaultValueHint != "_") {
            std::vector<float> numericDefault;
            std::size_t minimumComponents = 0U;
            std::size_t maximumComponents = 0U;
            switch (node.kind) {
            case RenderMaterialGraphNodeKind::ConstantScalar:
            case RenderMaterialGraphNodeKind::ParameterScalar:
                minimumComponents = maximumComponents = 1U;
                break;
            case RenderMaterialGraphNodeKind::ConstantVector2:
                minimumComponents = maximumComponents = 2U;
                break;
            case RenderMaterialGraphNodeKind::ConstantVector:
            case RenderMaterialGraphNodeKind::ParameterVector:
                minimumComponents = maximumComponents = 3U;
                break;
            case RenderMaterialGraphNodeKind::ConstantColor:
            case RenderMaterialGraphNodeKind::ParameterColor:
                minimumComponents = 3U;
                maximumComponents = 4U;
                break;
            default:
                break;
            }
            if (minimumComponents != 0U &&
                !ParseFiniteMaterialFloatSequence(
                    node.parameter.defaultValueHint,
                    numericDefault,
                    minimumComponents,
                    maximumComponents)) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "defaultValueHint",
                    "Material graph numeric default must contain exactly the expected number of finite components.");
            }
        }
        const RenderMaterialGraphNodeSupport pathSupport = RenderMaterialGraphNodeSupportForDocumentPath(graph, node.kind, renderPath);
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
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            ++outputNodeCount;
            if (outputNode == nullptr) {
                outputNode = &node;
            }
        }
        if (node.kind == RenderMaterialGraphNodeKind::CustomCode) {
            ValidateCustomCodeNode(node, diagnostics);
        }
        if (IsRenderMaterialGraphOrganizationNode(node.kind)) {
            const std::string& typeHint = node.parameter.defaultValueHint;
            if (!typeHint.empty() && !ParseRenderMaterialGraphPinType(typeHint).has_value()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::TypeMismatch,
                    node.id,
                    0U,
                    node.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage ? "output" : "input",
                    "Material graph organization node uses unsupported pass-through type '" + typeHint + "'.");
            }
        }
        if (IsRenderMaterialGraphPassThroughNode(node.kind) &&
            HasOutputLink(graph, node.id, "output") &&
            !HasInputLink(graph, node.id, "input")) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput,
                node.id,
                0U,
                "input",
                "Material graph pass-through node output is used but its input is disconnected.");
        }
        if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration) {
            const std::string key = NamedRerouteKey(node);
            if (key.empty()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "input",
                    "NamedRerouteDeclaration requires a stable reroute name.");
            } else if (NamedRerouteDeclarations(graph, key).size() > 1U) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "input",
                    "NamedRerouteDeclaration '" + key + "' is duplicated.");
            }
            if (!key.empty()) {
                bool used = false;
                for (const RenderMaterialGraphNode& candidate : graph.nodes) {
                    if (candidate.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage &&
                        NamedRerouteKey(candidate) == key) {
                        used = true;
                        break;
                    }
                }
                if (used && !HasInputLink(graph, node.id, "input")) {
                    AddGraphDiagnostic(
                        diagnostics,
                        RenderMaterialGraphDiagnosticSeverity::Error,
                        RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput,
                        node.id,
                        0U,
                        "input",
                        "NamedRerouteDeclaration '" + key + "' is used but its input is disconnected.");
                }
            }
        }
        if (node.kind == RenderMaterialGraphNodeKind::NamedRerouteUsage) {
            const std::string key = NamedRerouteKey(node);
            const std::vector<const RenderMaterialGraphNode*> declarations = NamedRerouteDeclarations(graph, key);
            if (key.empty()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "output",
                    "NamedRerouteUsage requires a stable reroute name.");
            } else if (declarations.empty()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "output",
                    "NamedRerouteUsage '" + key + "' has no matching declaration.");
            } else if (declarations.size() > 1U) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "output",
                    "NamedRerouteUsage '" + key + "' matches multiple declarations.");
            } else if (PassThroughPinType(*declarations.front()) != PassThroughPinType(node)) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::TypeMismatch,
                    node.id,
                    0U,
                    "output",
                    "NamedRerouteUsage '" + key + "' type does not match its declaration.");
            }
        }
        const bool textureSampleLocalSlot = IsTextureSampleNode(node.kind) && !HasInputLink(graph, node.id, "texture");
        const bool textureObjectParameterSlot = IsTextureObjectNode(node.kind);
        if (IsRenderMaterialGraphParameterNode(node.kind) || textureSampleLocalSlot || textureObjectParameterSlot) {
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
        if (node.kind == RenderMaterialGraphNodeKind::CollectionParameter) {
            const std::uint64_t collectionAssetId = RenderMaterialGraphCollectionAssetId(node);
            const std::string stableId = StableParameterId(node);
            if (collectionAssetId == 0U) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "collection",
                    "CollectionParameter node requires a material parameter collection asset id.");
            }
            if (stableId.empty()) {
                AddGraphDiagnostic(
                    diagnostics,
                    RenderMaterialGraphDiagnosticSeverity::Error,
                    RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                    node.id,
                    0U,
                    "stableId",
                    "CollectionParameter node requires a stable parameter id.");
            }
            if (collectionAssetId != 0U && !stableId.empty()) {
                const std::string key = std::to_string(collectionAssetId) + ":" + stableId;
                const auto [it, inserted] = collectionParameterStableIds.emplace(key, node.id);
                if (!inserted) {
                    AddGraphDiagnostic(
                        diagnostics,
                        RenderMaterialGraphDiagnosticSeverity::Error,
                        RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId,
                        node.id,
                        0U,
                        {},
                        "Material graph collection parameter '" + key + "' is already used by node " + std::to_string(it->second) + ".");
                }
            }
        }
        if (textureObjectParameterSlot || textureSampleLocalSlot) {
            const std::string textureRole = EffectiveTextureRoleForNode(graph, node);
            const RenderMaterialTextureColorSpace textureColorSpace = EffectiveTextureColorSpaceForNode(graph, node, textureRole);
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
    } else if (outputNodeCount != 1U) {
        AddGraphDiagnostic(
            diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
            outputNode->id,
            0U,
            {},
            "Material graph requires exactly one Material Output node; found " + std::to_string(outputNodeCount) + ".");
    }

    std::unordered_map<std::string, std::uint32_t> inputLinkOwners;
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
        if (!IsRenderMaterialGraphOutputPin(*fromNode, link.fromPin) ||
            !IsRenderMaterialGraphInputPin(*toNode, link.toPin) ||
            !AreRenderMaterialGraphPinsCompatible(*fromNode, link.fromPin, *toNode, link.toPin)) {
            const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(*fromNode, link.fromPin, true);
            const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(*toNode, link.toPin, false);
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::TypeMismatch,
                toNode->id,
                link.id,
                link.toPin,
                "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".");
        }

        const std::uint32_t targetPinId = link.toPinId != 0U
            ? link.toPinId
            : RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
        const std::string inputKey = std::to_string(link.toNodeId) + ":" + std::to_string(targetPinId);
        const auto [owner, inserted] = inputLinkOwners.emplace(inputKey, link.id);
        if (!inserted) {
            AddGraphDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticSeverity::Error,
                RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
                toNode->id,
                link.id,
                link.toPin,
                "Material graph input '" + link.toPin + "' has multiple incoming links (including link " +
                    std::to_string(owner->second) + "); each input accepts at most one link.");
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
            (!IsTextureSampleNode(node.kind) || HasInputLink(graph, node.id, "texture")) &&
            !IsTextureObjectNode(node.kind)) {
            continue;
        }

        const std::string stableId = StableParameterId(node);
        const std::string displayName = DisplayNameForParameter(node);
        if (IsTextureObjectNode(node.kind) || IsTextureSampleNode(node.kind)) {
            const std::string textureRole = EffectiveTextureRoleForNode(graph, node);
            schema.textureSlots.push_back(RenderMaterialTextureSlotSchema{
                .name = displayName,
                .stableId = stableId,
                .role = textureRole,
                .assetIdFieldName = TextureAssetFieldName(stableId),
                .pathFieldName = TexturePathFieldName(stableId),
                .expectedColorSpace = EffectiveTextureColorSpaceForNode(graph, node, textureRole),
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
    schema.unsupportedAdvancedFeatures = { "transparentRuntimePass", "tessellation" };

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

    std::vector<RenderMaterialTypePermutationKey> permutationKeys{
        RenderMaterialTypePermutationKey{ .name = "alphaMode", .defaultValue = "OPAQUE", .allowedValues = std::vector<std::string>{ "OPAQUE", "MASK", "BLEND" } },
        RenderMaterialTypePermutationKey{ .name = "doubleSided", .defaultValue = "false", .allowedValues = std::vector<std::string>{ "false", "true" } },
        RenderMaterialTypePermutationKey{ .name = "graphSourceHash", .defaultValue = std::to_string(compile.shader.sourceHash), .allowedValues = std::vector<std::string>{ std::to_string(compile.shader.sourceHash) } },
    };
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::QualitySwitch)) {
        permutationKeys.push_back(RenderMaterialTypePermutationKey{
            .name = "qualityLevel",
            .defaultValue = std::string{ RenderMaterialGraphQualityLevelPinName(context.qualityLevel) },
            .allowedValues = std::vector<std::string>{ "low", "med", "high", "epic" },
        });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::FeatureLevelSwitch)) {
        permutationKeys.push_back(RenderMaterialTypePermutationKey{
            .name = "featureLevel",
            .defaultValue = std::string{ RenderMaterialGraphFeatureLevelPinName(context.featureLevel) },
            .allowedValues = std::vector<std::string>{ "es3", "sm5", "sm6" },
        });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::ShadingPathSwitch)) {
        permutationKeys.push_back(RenderMaterialTypePermutationKey{
            .name = "shadingPath",
            .defaultValue = std::string{ RenderMaterialGraphShadingPathPinName(context.shadingPath) },
            .allowedValues = std::vector<std::string>{ "forward", "forwardPlus", "deferred" },
        });
    }
    if (GraphContainsNodeKind(graph, RenderMaterialGraphNodeKind::ShaderStageSwitch)) {
        permutationKeys.push_back(RenderMaterialTypePermutationKey{
            .name = "shaderStage",
            .defaultValue = std::string{ RenderMaterialGraphShaderStagePinName(context.shaderStage) },
            .allowedValues = std::vector<std::string>{ "fragment", "vertex" },
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
            RenderMaterialTypeRenderPass{ .name = "GBuffer", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = graphFragmentShader },
            RenderMaterialTypeRenderPass{ .name = "ShadowDepth", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_shadow_instanced", .fragmentShader = "fs_mesh_shadow_instanced" },
            RenderMaterialTypeRenderPass{ .name = "BaseTransparent", .support = RenderMaterialFeatureSupport::Supported, .vertexShader = "vs_mesh_instanced", .fragmentShader = graphFragmentShader },
        },
        .permutationKeys = std::move(permutationKeys),
        .requiredResources = std::move(requiredResources),
        .schema = std::move(schema),
    };
    return result;
}

MaterialSurface DefaultMaterialSurface() noexcept {
    return {};
}

MaterialGraphContext DefaultMaterialGraphContext() noexcept {
    return {};
}

} // namespace kb::render
