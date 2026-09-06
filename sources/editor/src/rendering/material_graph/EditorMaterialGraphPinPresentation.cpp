#include "rendering/material_graph/EditorMaterialGraphPinPresentation.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace kb::editor {
namespace {

// Turn a camelCase pin name (e.g. "baseColor") into a readable label ("Base Color"). Used as the fallback
// label for nodes that are not in the editor's explicit pin tables, so every node renders sensible pins.
[[nodiscard]] std::string HumanizePinName(std::string_view pin) {
    std::string label;
    label.reserve(pin.size() + 4U);
    for (std::size_t i = 0U; i < pin.size(); ++i) {
        const char c = pin[i];
        if (i == 0U) {
            label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else if (c >= 'A' && c <= 'Z') {
            label.push_back(' ');
            label.push_back(c);
        } else {
            label.push_back(c);
        }
    }
    return label.empty() ? std::string{pin} : label;
}

[[nodiscard]] std::vector<EditorMaterialGraphPinPresentation> GraphPinFallback(std::vector<std::string> names) {
    std::vector<EditorMaterialGraphPinPresentation> pins;
    pins.reserve(names.size());
    for (std::string& name : names) {
        std::string label = HumanizePinName(name);
        pins.emplace_back(std::move(name), std::move(label));
    }
    return pins;
}

[[nodiscard]] std::vector<EditorMaterialGraphPinPresentation>
PreferredInputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {
            {"baseColor", "Base Color"},     {"normal", "Normal"},
            {"roughness", "Roughness"},      {"metallic", "Metallic"},
            {"specular", "Specular"},        {"emissive", "Emissive"},
            {"occlusion", "Occlusion"},      {"alpha", "Alpha"},
            {"alphaClipThreshold", "Clip"},  {"tangentOutput", "Tangent Out"},
            {"attributes", "Attributes"},    {"worldPositionOffset", "WPO"},
            {"customizedUv0", "Custom UV0"}, {"displacement", "Displacement"},
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
        return {{"texture", "Tex."}, {"uv", "UV"}};
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
        return {{"texture", "Cube"}, {"direction", "Dir"}};
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
        return {{"texture", "3D"}, {"uvw", "UVW"}};
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return {{"texture", "Array"}, {"uv", "UV"}, {"layer", "Layer"}};
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return {{"input", "In"}};
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return {{"input", "In"}};
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::FunctionOutput:
        return {{"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::Add:
    case kb::render::RenderMaterialGraphNodeKind::Subtract:
    case kb::render::RenderMaterialGraphNodeKind::Multiply:
    case kb::render::RenderMaterialGraphNodeKind::Divide:
    case kb::render::RenderMaterialGraphNodeKind::Minimum:
    case kb::render::RenderMaterialGraphNodeKind::Maximum:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
        return {{"a", "A"}, {"b", "B"}};
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
        return {{"a", "A"}, {"b", "B"}, {"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::Power:
        return {{"base", "Base"}, {"exponent", "Exponent"}};
    case kb::render::RenderMaterialGraphNodeKind::OneMinus:
    case kb::render::RenderMaterialGraphNodeKind::Absolute:
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
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return {{"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
        return {{"value", "Temp (K)"}};
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
        return {{"value", "Position"}};
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
        return {{"cell", "Cell"}, {"index", "Index"}, {"seed", "Seed"}};
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
        return {{"a", "XYZ"}, {"b", "W"}};
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
        return {{"value", "Gradient"}};
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
        return {{"value", "Mask"}};
    case kb::render::RenderMaterialGraphNodeKind::Transform:
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
        return {{"value", "Vector"}};
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
        return {{"x", "X"}, {"y", "Y"}, {"z", "Z"}, {"w", "W"}};
    case kb::render::RenderMaterialGraphNodeKind::Step:
        return {{"edge", "Edge"}, {"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
        return {{"min", "Min"}, {"max", "Max"}, {"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::If:
        return {{"a", "A"}, {"b", "B"}, {"less", "Less"}, {"equal", "Equal"}, {"greater", "Greater"}};
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
        return {{"index", "Index"},  {"default", "Default"}, {"case0", "Case 0"},
                {"case1", "Case 1"}, {"case2", "Case 2"},    {"case3", "Case 3"}};
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return {{"color", "Color"}, {"fraction", "Fraction"}};
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
        return {{"normal", "Normal"}, {"view", "View"}, {"exponent", "Exponent"}, {"base", "Base"}};
    case kb::render::RenderMaterialGraphNodeKind::Negate:
    case kb::render::RenderMaterialGraphNodeKind::Sign:
    case kb::render::RenderMaterialGraphNodeKind::Round:
    case kb::render::RenderMaterialGraphNodeKind::Truncate:
    case kb::render::RenderMaterialGraphNodeKind::Tangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSine:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosine:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent:
    case kb::render::RenderMaterialGraphNodeKind::ArcSineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcCosineFast:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangentFast:
        return {{"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2:
    case kb::render::RenderMaterialGraphNodeKind::ArcTangent2Fast:
        return {{"y", "Y"}, {"x", "X"}};
    case kb::render::RenderMaterialGraphNodeKind::Clamp:
        return {{"value", "Value"}, {"min", "Min"}, {"max", "Max"}};
    case kb::render::RenderMaterialGraphNodeKind::Lerp:
        return {{"a", "A"}, {"b", "B"}, {"t", "T"}};
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return {{"color", "Color"}};
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
        return {};
    default:
        break;
    }
    return {};
}

[[nodiscard]] std::vector<EditorMaterialGraphPinPresentation>
PreferredOutputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case kb::render::RenderMaterialGraphNodeKind::ConstantScalar:
    case kb::render::RenderMaterialGraphNodeKind::ConstantBool:
    case kb::render::RenderMaterialGraphNodeKind::ParameterScalar:
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
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeX:
    case kb::render::RenderMaterialGraphNodeKind::PartialDerivativeY:
    case kb::render::RenderMaterialGraphNodeKind::BlackBody:
    case kb::render::RenderMaterialGraphNodeKind::Noise:
    case kb::render::RenderMaterialGraphNodeKind::VectorNoise:
    case kb::render::RenderMaterialGraphNodeKind::Sobol:
    case kb::render::RenderMaterialGraphNodeKind::ColorRamp:
    case kb::render::RenderMaterialGraphNodeKind::AntialiasedTextureMask:
    case kb::render::RenderMaterialGraphNodeKind::Transform:
    case kb::render::RenderMaterialGraphNodeKind::TransformPosition:
    case kb::render::RenderMaterialGraphNodeKind::Fmod:
    case kb::render::RenderMaterialGraphNodeKind::InverseLerp:
    case kb::render::RenderMaterialGraphNodeKind::SphereMask:
    case kb::render::RenderMaterialGraphNodeKind::AppendVector:
    case kb::render::RenderMaterialGraphNodeKind::DotProduct:
    case kb::render::RenderMaterialGraphNodeKind::CrossProduct:
    case kb::render::RenderMaterialGraphNodeKind::Normalize:
    case kb::render::RenderMaterialGraphNodeKind::Length:
    case kb::render::RenderMaterialGraphNodeKind::Distance:
    case kb::render::RenderMaterialGraphNodeKind::MakeVector:
    case kb::render::RenderMaterialGraphNodeKind::Step:
    case kb::render::RenderMaterialGraphNodeKind::SmoothStep:
    case kb::render::RenderMaterialGraphNodeKind::If:
    case kb::render::RenderMaterialGraphNodeKind::RuntimeSwitch:
    case kb::render::RenderMaterialGraphNodeKind::Fresnel:
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
        return {{"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::Desaturate:
        return {{"color", "Color"}};
    case kb::render::RenderMaterialGraphNodeKind::BreakVector:
        return {{"x", "X"}, {"y", "Y"}, {"z", "Z"}, {"w", "W"}};
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector2:
        return {{"xy", "XY"}};
    case kb::render::RenderMaterialGraphNodeKind::ConstantVector:
        return {{"xyz", "RGB"}, {"r", "R"}, {"g", "G"}, {"b", "B"}};
    case kb::render::RenderMaterialGraphNodeKind::ParameterVector:
        return {{"xyz", "XYZ"}};
    case kb::render::RenderMaterialGraphNodeKind::ConstantColor:
    case kb::render::RenderMaterialGraphNodeKind::ParameterColor:
        return {{"rgba", "RGBA"}, {"r", "R"}, {"g", "G"}, {"b", "B"}, {"a", "A"}};
    case kb::render::RenderMaterialGraphNodeKind::CollectionParameter:
        return {
            {"value", "Value"}, {"scalar", "Scalar"}, {"xyz", "XYZ"}, {"rgba", "RGBA"},
            {"r", "R"},         {"g", "G"},           {"b", "B"},     {"a", "A"},
        };
    case kb::render::RenderMaterialGraphNodeKind::TextureSample:
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case kb::render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case kb::render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return {{"color", "RGBA"}, {"r", "R"}, {"g", "G"}, {"b", "B"}, {"a", "A"}};
    case kb::render::RenderMaterialGraphNodeKind::Reroute:
    case kb::render::RenderMaterialGraphNodeKind::CompositeInput:
    case kb::render::RenderMaterialGraphNodeKind::CompositeOutput:
        return {{"output", "Out"}};
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteUsage:
        return {{"output", "Out"}};
    case kb::render::RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
        return {};
    case kb::render::RenderMaterialGraphNodeKind::FunctionInput:
        return {{"value", "Value"}};
    case kb::render::RenderMaterialGraphNodeKind::ParameterTexture:
    case kb::render::RenderMaterialGraphNodeKind::TextureObject:
        return {{"texture", "Tex."}};
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectCube:
        return {{"texture", "Cube"}};
    case kb::render::RenderMaterialGraphNodeKind::TextureObjectVolume:
        return {{"texture", "3D"}};
    case kb::render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return {{"texture", "Array"}};
    case kb::render::RenderMaterialGraphNodeKind::NormalUnpack:
        return {{"normal", "Normal"}};
    case kb::render::RenderMaterialGraphNodeKind::Uv:
        return {{"uv", "UV"}};
    case kb::render::RenderMaterialGraphNodeKind::LayerStack:
        return {{"attributes", "Attributes"}};
    case kb::render::RenderMaterialGraphNodeKind::MaterialOutput:
        return {};
    default:
        break;
    }
    return {};
}

[[nodiscard]] std::vector<EditorMaterialGraphPinPresentation>
ReconcileWithSchema(std::vector<EditorMaterialGraphPinPresentation> preferred,
                    std::vector<std::string> canonicalNames) {
    std::vector<EditorMaterialGraphPinPresentation> reconciled;
    reconciled.reserve(canonicalNames.size());

    for (EditorMaterialGraphPinPresentation& pin : preferred) {
        const bool isCanonical = std::ranges::find(canonicalNames, pin.name) != canonicalNames.end();
        const bool alreadyAdded =
            std::ranges::find_if(reconciled, [&pin](const EditorMaterialGraphPinPresentation& candidate) {
                return candidate.name == pin.name;
            }) != reconciled.end();
        if (isCanonical && !alreadyAdded) {
            reconciled.push_back(std::move(pin));
        }
    }

    for (std::string& name : canonicalNames) {
        const bool alreadyAdded =
            std::ranges::find_if(reconciled, [&name](const EditorMaterialGraphPinPresentation& candidate) {
                return candidate.name == name;
            }) != reconciled.end();
        if (!alreadyAdded) {
            std::string label = HumanizePinName(name);
            reconciled.emplace_back(std::move(name), std::move(label));
        }
    }
    return reconciled;
}

[[nodiscard]] std::vector<std::string> PinNames(std::vector<EditorMaterialGraphPinPresentation> pins) {
    std::vector<std::string> names;
    names.reserve(pins.size());
    for (EditorMaterialGraphPinPresentation& pin : pins) {
        names.push_back(std::move(pin.name));
    }
    return names;
}

} // namespace

std::vector<EditorMaterialGraphPinPresentation>
EditorMaterialGraphPinPresentationCatalog::InputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    return ReconcileWithSchema(PreferredInputPins(kind), kb::render::RenderMaterialGraphNodeInputPinNames(kind));
}

std::vector<EditorMaterialGraphPinPresentation>
EditorMaterialGraphPinPresentationCatalog::InputPins(const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return GraphPinFallback(kb::render::RenderMaterialGraphNodeInputPinNames(node));
    }
    return InputPins(node.kind);
}

std::vector<EditorMaterialGraphPinPresentation>
EditorMaterialGraphPinPresentationCatalog::OutputPins(kb::render::RenderMaterialGraphNodeKind kind) {
    return ReconcileWithSchema(PreferredOutputPins(kind), kb::render::RenderMaterialGraphNodeOutputPinNames(kind));
}

std::vector<EditorMaterialGraphPinPresentation>
EditorMaterialGraphPinPresentationCatalog::OutputPins(const kb::render::RenderMaterialGraphNode& node) {
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::CustomCode ||
        node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall) {
        return GraphPinFallback(kb::render::RenderMaterialGraphNodeOutputPinNames(node));
    }
    return OutputPins(node.kind);
}

std::vector<std::string>
EditorMaterialGraphPinPresentationCatalog::InputPinNames(kb::render::RenderMaterialGraphNodeKind kind) {
    return PinNames(InputPins(kind));
}

std::vector<std::string>
EditorMaterialGraphPinPresentationCatalog::OutputPinNames(kb::render::RenderMaterialGraphNodeKind kind) {
    return PinNames(OutputPins(kind));
}

} // namespace kb::editor
