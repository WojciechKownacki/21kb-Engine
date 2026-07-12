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
    case RenderMaterialGraphPinType::TextureCube:
        return "textureCube";
    case RenderMaterialGraphPinType::Texture3D:
        return "texture3D";
    case RenderMaterialGraphPinType::Texture2DArray:
        return "texture2DArray";
    case RenderMaterialGraphPinType::Sampler:
        return "sampler";
    case RenderMaterialGraphPinType::Normal:
        return "normal";
    case RenderMaterialGraphPinType::Bool:
        return "bool";
    case RenderMaterialGraphPinType::MaterialAttributes:
        return "materialAttributes";
    }
    return "unknown";
}

std::optional<RenderMaterialGraphPinType> ParseRenderMaterialGraphPinType(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "float") || EqualsIgnoreCase(text, "scalar")) {
        return RenderMaterialGraphPinType::Float;
    }
    if (EqualsIgnoreCase(text, "float2") || EqualsIgnoreCase(text, "vec2")) {
        return RenderMaterialGraphPinType::Float2;
    }
    if (EqualsIgnoreCase(text, "float3") || EqualsIgnoreCase(text, "vec3")) {
        return RenderMaterialGraphPinType::Float3;
    }
    if (EqualsIgnoreCase(text, "float4") || EqualsIgnoreCase(text, "vec4")) {
        return RenderMaterialGraphPinType::Float4;
    }
    if (EqualsIgnoreCase(text, "color")) {
        return RenderMaterialGraphPinType::Color;
    }
    if (EqualsIgnoreCase(text, "texture2D")) {
        return RenderMaterialGraphPinType::Texture2D;
    }
    if (EqualsIgnoreCase(text, "textureCube")) {
        return RenderMaterialGraphPinType::TextureCube;
    }
    if (EqualsIgnoreCase(text, "texture3D") || EqualsIgnoreCase(text, "textureVolume")) {
        return RenderMaterialGraphPinType::Texture3D;
    }
    if (EqualsIgnoreCase(text, "texture2DArray")) {
        return RenderMaterialGraphPinType::Texture2DArray;
    }
    if (EqualsIgnoreCase(text, "sampler")) {
        return RenderMaterialGraphPinType::Sampler;
    }
    if (EqualsIgnoreCase(text, "normal")) {
        return RenderMaterialGraphPinType::Normal;
    }
    if (EqualsIgnoreCase(text, "bool")) {
        return RenderMaterialGraphPinType::Bool;
    }
    if (EqualsIgnoreCase(text, "materialAttributes") || EqualsIgnoreCase(text, "MaterialSurface")) {
        return RenderMaterialGraphPinType::MaterialAttributes;
    }
    if (EqualsIgnoreCase(text, "unknown")) {
        return RenderMaterialGraphPinType::Unknown;
    }
    return std::nullopt;
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
    if ((from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Float3) ||
        (from == RenderMaterialGraphPinType::Float3 && to == RenderMaterialGraphPinType::Float2) ||
        (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Float4) ||
        (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Float2) ||
        (from == RenderMaterialGraphPinType::Float2 && to == RenderMaterialGraphPinType::Color) ||
        (from == RenderMaterialGraphPinType::Color && to == RenderMaterialGraphPinType::Float2)) {
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
    if ((from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Float) ||
        (from == RenderMaterialGraphPinType::Float && to == RenderMaterialGraphPinType::Bool) ||
        (from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Float4) ||
        (from == RenderMaterialGraphPinType::Float4 && to == RenderMaterialGraphPinType::Bool) ||
        (from == RenderMaterialGraphPinType::Bool && to == RenderMaterialGraphPinType::Color)) {
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

bool AreRenderMaterialGraphPinsCompatible(
    const RenderMaterialGraphNode& fromNode,
    std::string_view fromPin,
    const RenderMaterialGraphNode& toNode,
    std::string_view toPin) noexcept {
    return AreRenderMaterialGraphPinsCompatible(
        RenderMaterialGraphPinDataType(fromNode, fromPin, true),
        RenderMaterialGraphPinDataType(toNode, toPin, false));
}

} // namespace kb::render
