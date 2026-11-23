#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include <cmath>
#include <cstdlib>
#include "CliCommon.hpp"
#include "CliMaterialShared.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/ColorSpace.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialFunctionLibrary.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderMaterialAuthoring.hpp"
#include "kb/render/resources/RenderMaterialGraphCost.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphNodeDocs.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// material-graph: the Material Editor's graph edits from the command line (add a node, connect
// pins, set a value, promote a constant to a parameter, rename), through the same document
// functions the editor uses (RenderMaterialAuthoring), saved and checked the way the editor's
// Save checks a material. material-instance-new|set|show: the editor's Create Material Instance
// and its override rows.

namespace kb::cli {
namespace {

constexpr const char* kMaterialExtension = ".kbmat";
constexpr const char* kInstanceExtension = ".kbmatinst";

struct LoadedProject {
    std::filesystem::path root;
    kb::scene::Scene scene;
};

[[nodiscard]] bool Mount(LoadedProject& project, CommandIo io) {
    std::string error;
    if (!MountProjectAssets(project.scene, project.root, error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] const kb::assets::AssetRegistry& Registry(const LoadedProject& project) {
    return project.scene.Assets().Manager().Registry();
}

// The node a command names: its id, its name (as the graph shows it), its parameter id, or
// Output for the Material Output node.
[[nodiscard]] const kb::render::RenderMaterialGraphNode* FindNode(
    const kb::render::RenderMaterialGraphDocument& graph, std::string_view reference) {
    std::uint32_t id = 0U;
    const auto [end, error] = std::from_chars(reference.data(), reference.data() + reference.size(), id);
    if (error == std::errc{} && end == reference.data() + reference.size()) {
        return kb::render::FindRenderMaterialGraphNode(graph, id);
    }
    const std::string wanted = material::Normalized(reference);
    if (wanted == "output" || wanted == "materialoutput") {
        const auto output = std::ranges::find_if(graph.nodes, [](const kb::render::RenderMaterialGraphNode& node) {
            return node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput;
        });
        return output != graph.nodes.end() ? &*output : nullptr;
    }
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if ((!node.parameter.displayName.empty() && material::Normalized(node.parameter.displayName) == wanted) ||
            (!node.parameter.stableId.empty() && material::Normalized(node.parameter.stableId) == wanted)) {
            return &node;
        }
    }
    return nullptr;
}

// Names for the common nodes, beside the engine's own kind names.
[[nodiscard]] std::optional<kb::render::RenderMaterialGraphNodeKind> ParseKind(std::string_view text) {
    if (const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = kb::render::ParseRenderMaterialGraphNodeKind(text)) {
        return kind;
    }
    const std::string wanted = material::Normalized(text);
    using Kind = kb::render::RenderMaterialGraphNodeKind;
    if (wanted == "scalarparameter") return Kind::ParameterScalar;
    if (wanted == "vectorparameter" || wanted == "colorparameter") return Kind::ParameterColor;
    if (wanted == "textureparameter" || wanted == "texturesampleparameter2d") return Kind::ParameterTexture;
    if (wanted == "constant") return Kind::ConstantScalar;
    if (wanted == "constant3vector") return Kind::ConstantVector;
    if (wanted == "constant4vector") return Kind::ConstantColor;
    for (const Kind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
        if (material::Normalized(kb::render::RenderMaterialGraphNodeKindName(kind)) == wanted) return kind;
    }
    return std::nullopt;
}

[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
    return joined.empty() ? "none" : joined;
}

[[nodiscard]] std::string NodeLabel(const kb::render::RenderMaterialGraphNode& node) {
    std::string label = std::to_string(node.id) + " " + std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) };
    if (!node.parameter.displayName.empty()) label += " '" + node.parameter.displayName + "'";
    if (kb::render::IsRenderMaterialGraphParameterNode(node.kind) && !node.parameter.stableId.empty()) {
        label += " (" + node.parameter.stableId + ")";
    }
    return label;
}

// A colour as material-set takes it (#RRGGBB sRGB, or linear r,g,b[,a]) becomes the "r g b a"
// a graph value is written as; other text is passed on.
[[nodiscard]] std::string GraphValueText(std::string_view text, std::size_t colorChannels) {
    if (colorChannels == 0U) return std::string{ text };
    if (const std::optional<std::array<float, 4>> parsed = ParseColorText(text, colorChannels)) {
        std::ostringstream joined;
        joined << (*parsed)[0] << ' ' << (*parsed)[1] << ' ' << (*parsed)[2];
        if (colorChannels == 4U) joined << ' ' << (*parsed)[3];
        return joined.str();
    }
    return std::string{ text };
}

[[nodiscard]] std::string ParameterValueText(const kb::render::RenderMaterialGraphParameterValue& value, const kb::assets::AssetRegistry& registry) {
    std::ostringstream text;
    switch (value.type) {
    case kb::render::RenderMaterialParameterType::Scalar:
        text << value.numbers[0];
        break;
    case kb::render::RenderMaterialParameterType::Vec3:
        text << value.numbers[0] << ' ' << value.numbers[1] << ' ' << value.numbers[2];
        break;
    case kb::render::RenderMaterialParameterType::Vec4:
    case kb::render::RenderMaterialParameterType::Color:
        text << value.numbers[0] << ' ' << value.numbers[1] << ' ' << value.numbers[2] << ' ' << value.numbers[3];
        break;
    case kb::render::RenderMaterialParameterType::Bool:
        text << (value.boolValue ? "true" : "false");
        break;
    case kb::render::RenderMaterialParameterType::Enum:
        text << value.text;
        break;
    case kb::render::RenderMaterialParameterType::Texture: {
        const std::string path = material::TexturePath(registry, value.assetId);
        text << (value.assetId == 0U ? std::string{ "none" } : path.empty() ? "missing texture " + std::to_string(value.assetId) : path);
        if (value.numbers[0] >= 0.5F) {
            text << "@uv1";  // the slot samples UV1
        }
        break;
    }
    }
    return text.str();
}

// The parameters a material exposes: name, id and type.
struct ExposedParameter {
    std::string displayName;
    std::string stableId;
    kb::render::RenderMaterialParameterType type = kb::render::RenderMaterialParameterType::Scalar;
};

[[nodiscard]] std::vector<ExposedParameter> ExposedParameters(const kb::render::RenderMaterialAssetData& asset) {
    const kb::render::RenderMaterialTypeSchema schema = kb::render::BuildRenderMaterialGraphParameterSchema(asset.graph, "kb_cli", 1U);
    std::vector<ExposedParameter> parameters;
    for (const kb::render::RenderMaterialParameterSchema& parameter : schema.parameters) {
        parameters.push_back(ExposedParameter{ parameter.displayName, parameter.name, parameter.type });
    }
    for (const kb::render::RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        parameters.push_back(ExposedParameter{ slot.name, slot.stableId, kb::render::RenderMaterialParameterType::Texture });
    }
    return parameters;
}

[[nodiscard]] const ExposedParameter* FindParameter(const std::vector<ExposedParameter>& parameters, std::string_view name) {
    const std::string wanted = material::Normalized(name);
    const auto found = std::ranges::find_if(parameters, [&](const ExposedParameter& parameter) {
        return material::Normalized(parameter.displayName) == wanted || material::Normalized(parameter.stableId) == wanted;
    });
    return found != parameters.end() ? &*found : nullptr;
}

[[nodiscard]] std::string ParameterList(const std::vector<ExposedParameter>& parameters) {
    std::vector<std::string> names;
    for (const ExposedParameter& parameter : parameters) names.push_back(parameter.displayName);
    return JoinNames(names);
}

// A graph parameter value from text: a texture path for a texture, else as the Material Editor's
// parameter row parses it (colours also as #RRGGBB).
[[nodiscard]] std::optional<kb::render::RenderMaterialGraphParameterValue> ParameterValue(
    const ExposedParameter& parameter, std::string_view text, const material::TextureResolver& textures, std::string& error) {
    if (parameter.type == kb::render::RenderMaterialParameterType::Texture) {
        // "<texture>@uv1" (or @uv0) also picks the UV channel the slot samples.
        float uvChannel = 0.0F;
        if (text.ends_with("@uv1") || text.ends_with("@uv0")) {
            uvChannel = text.ends_with("@uv1") ? 1.0F : 0.0F;
            text.remove_suffix(4U);
        }
        const std::optional<std::uint64_t> id = textures(text, error);
        if (!id.has_value()) return std::nullopt;
        return kb::render::RenderMaterialGraphParameterValue{
            .stableId = parameter.stableId, .type = kb::render::RenderMaterialParameterType::Texture,
            .numbers = { uvChannel, 0.0F, 0.0F, 0.0F }, .assetId = *id };
    }
    const std::size_t channels = parameter.type == kb::render::RenderMaterialParameterType::Color ||
            parameter.type == kb::render::RenderMaterialParameterType::Vec4 ? 4U
        : parameter.type == kb::render::RenderMaterialParameterType::Vec3 ? 3U : 0U;
    std::optional<kb::render::RenderMaterialGraphParameterValue> value =
        kb::render::ParseRenderMaterialGraphParameterValue(parameter.stableId, parameter.type, GraphValueText(text, channels));
    if (!value.has_value()) {
        error = parameter.displayName + ": '" + std::string{ text } + "' is not a value of this parameter";
    }
    return value;
}

// Problems the editor's Save reports: a document that would not load again refuses the write;
// graph and shader-generation errors are reported with it (such a material is saved too,
// and the editor shows the same errors).
struct MaterialCheck {
    bool loads = true;
    std::vector<std::string> problems;
};

// "node <label>[ pin <pin>]: " for a diagnostic about a node, so the message names what to change.
[[nodiscard]] std::string DiagnosticWhere(const kb::render::RenderMaterialGraphDocument& graph, const kb::render::RenderMaterialGraphDiagnostic& diagnostic) {
    const kb::render::RenderMaterialGraphNode* node = diagnostic.nodeId != 0U ? kb::render::FindRenderMaterialGraphNode(graph, diagnostic.nodeId) : nullptr;
    if (node == nullptr) return {};
    return "node " + NodeLabel(*node) + (diagnostic.pin.empty() ? std::string{} : " pin " + diagnostic.pin) + ": ";
}

[[nodiscard]] MaterialCheck CheckMaterial(const kb::render::RenderMaterialAssetData& asset, const kb::assets::AssetRegistry& registry) {
    MaterialCheck check{};
    std::ostringstream serialized;
    kb::render::RenderMaterialAssetWriter::Write(serialized, asset);
    std::istringstream input{ serialized.str() };
    const kb::render::RenderMaterialAssetParseResult parsed = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : parsed.diagnostics) {
        if (diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error) {
            check.loads = false;
            check.problems.push_back(std::string{ kb::render::RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message);
        }
    }
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : kb::render::ValidateRenderMaterialAssetGraphDiagnostics(asset)) {
        if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
            check.problems.push_back(std::string{ kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind) } + ": " + DiagnosticWhere(asset.graph, diagnostic) + diagnostic.message);
        }
    }
    if (kb::render::HasGraphAuthoringData(asset.graph)) {
        const kb::render::RenderMaterialGraphFunctionLibrary library = kb::render::BuildRenderMaterialFunctionLibraryFromFiles(registry, asset.graph).library;
        kb::render::RenderMaterialGraphBuildContext context;
        context.functionLibrary = &library;
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(asset.graph, context);
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
            if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
                check.problems.push_back("shader: " + DiagnosticWhere(asset.graph, diagnostic) + diagnostic.message);
            }
        }
    }
    return check;
}

[[nodiscard]] bool SaveMaterial(const kb::render::RenderMaterialAssetData& asset, const material::ProjectAsset& located, const kb::assets::AssetRegistry& registry, CommandIo io) {
    const MaterialCheck check = CheckMaterial(asset, registry);
    if (!check.loads) {
        io.err << "error: the material would not load again; nothing was written:\n";
        for (const std::string& problem : check.problems) io.err << "  " << problem << '\n';
        return false;
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(located.file, asset)) {
        io.err << "error: the material could not be written: " << located.virtualPath << '\n';
        return false;
    }
    for (const std::string& problem : check.problems) io.err << "warning: " << problem << '\n';
    return true;
}

// The value of a node: a constant's own value, a texture node's texture, or a parameter's value
// in the material (graphParameterValues), as the Material Editor sets each.
[[nodiscard]] std::string SetNodeValue(
    kb::render::RenderMaterialAssetData& asset,
    const kb::render::RenderMaterialGraphNode& node,
    std::string_view text,
    const material::TextureResolver& textures) {
    if (kb::render::IsRenderMaterialGraphConstantNode(node.kind)) {
        const std::size_t channels = node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantColor ? 4U
            : node.kind == kb::render::RenderMaterialGraphNodeKind::ConstantVector ? 3U : 0U;
        const std::string value = GraphValueText(text, channels);
        return kb::render::SetRenderMaterialGraphConstantValue(asset.graph, node.id, value)
            ? std::string{} : "node " + NodeLabel(node) + " does not take '" + std::string{ text } + "'";
    }
    if (kb::render::IsRenderMaterialGraphTextureAssetNode(node.kind)) {
        std::string error;
        const std::optional<std::uint64_t> id = textures(text, error);
        if (!id.has_value()) return error;
        return kb::render::SetRenderMaterialGraphTextureNodeAsset(asset, node.id, *id)
            ? std::string{} : "node " + NodeLabel(node) + " does not take a texture";
    }
    if (kb::render::IsRenderMaterialGraphParameterNode(node.kind)) {
        const std::vector<ExposedParameter> parameters = ExposedParameters(asset);
        const ExposedParameter* parameter = FindParameter(parameters, node.parameter.stableId);
        if (parameter == nullptr) return "node " + NodeLabel(node) + " exposes no parameter value";
        std::string error;
        std::optional<kb::render::RenderMaterialGraphParameterValue> value = ParameterValue(*parameter, text, textures, error);
        if (!value.has_value()) return error;
        kb::render::UpsertRenderMaterialGraphParameterValue(asset, std::move(*value));
        return {};
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
        return "node " + NodeLabel(node) + " has no value to set";
    }
    if (node.kind == kb::render::RenderMaterialGraphNodeKind::Noise) {
        // Noise settings: "<function> <turbulence> <scale> <quality> <levels> <level scale> <min> <max>".
        if (const kb::render::RenderMaterialGraphNoiseSettingsParseResult noise = kb::render::ParseRenderMaterialGraphNoiseSettings(text);
            !noise.settings.has_value()) {
            return "node " + NodeLabel(node) + ": " + noise.error;
        }
    }
    // Every other node keeps its settings (Panner speed, Rotator speed and centre, Texture
    // Coordinate tiling, Fresnel exponent, ...) as the space-separated value the Material
    // Editor's node properties write; the saved material is compiled before it is kept.
    for (kb::render::RenderMaterialGraphNode& candidate : asset.graph.nodes) {
        if (candidate.id == node.id) {
            candidate.parameter.defaultValueHint = std::string{ text };
            return {};
        }
    }
    return "node " + NodeLabel(node) + " has no value to set";
}

// The values of a setting, for the messages that refuse one.
template <typename Enum, std::size_t Count>
[[nodiscard]] std::string SettingValues(std::string_view (*name)(Enum)) {
    std::string list;
    for (std::size_t index = 0U; index < Count; ++index) list += (list.empty() ? "" : ", ") + std::string{ name(static_cast<Enum>(index)) };
    return list;
}

// The material-wide settings of the Material Editor's Details: Domain, Shading Model, Blend Mode.
[[nodiscard]] std::string SetGraphSetting(kb::render::RenderMaterialAssetData& asset, std::string_view name, std::string_view value) {
    const std::string wanted = material::Normalized(name);
    const std::string text = material::Normalized(value);
    if (wanted == "domain") {
        const std::string_view canonical = kb::render::RenderMaterialDomainName(kb::render::ParseRenderMaterialDomain(value));
        if (material::Normalized(canonical) != text) {
            return "Domain does not take '" + std::string{ value } + "'; values: " + SettingValues<kb::render::RenderMaterialDomain, 6U>(&kb::render::RenderMaterialDomainName);
        }
        asset.graph.materialDomain = std::string{ canonical };
        return {};
    }
    if (wanted == "shadingmodel") {
        const std::string_view canonical = kb::render::RenderMaterialShadingModelName(kb::render::ParseRenderMaterialShadingModel(value));
        if (material::Normalized(canonical) != text) {
            return "Shading Model does not take '" + std::string{ value } + "'; values: " + SettingValues<kb::render::RenderMaterialShadingModel, 9U>(&kb::render::RenderMaterialShadingModelName);
        }
        asset.graph.shadingModel = std::string{ canonical };
        return {};
    }
    if (wanted == "blendmode") {
        const std::string_view canonical = kb::render::RenderMaterialGraphBlendModeName(kb::render::ParseRenderMaterialGraphBlendMode(value));
        if (material::Normalized(canonical) != text) {
            return "Blend Mode does not take '" + std::string{ value } + "'; values: " + SettingValues<kb::render::RenderMaterialGraphBlendMode, 7U>(&kb::render::RenderMaterialGraphBlendModeName);
        }
        asset.graph.blendMode = std::string{ canonical };
        return {};
    }
    if (wanted == "maxworldpositionoffsetdisplacement") {
        const std::string number{ value };
        char* end = nullptr;
        const float margin = std::strtof(number.c_str(), &end);
        if (number.empty() || end == number.c_str() || *end != '\0' || !std::isfinite(margin) || margin < 0.0F) {
            return "Max World Position Offset Displacement takes a value of 0 or more, not '" + number + "'";
        }
        asset.graph.maxWorldPositionOffsetDisplacement = margin;
        return {};
    }
    return "material-graph setting --name takes Domain, Shading Model, Blend Mode or Max World Position Offset Displacement, not '" +
        std::string{ name } + "'";
}

// ---- Graph text -----------------------------------------------------------------------------------------------------
// A whole graph as short text, written in one call (material-graph text, material-new --graph) and printed back by
// `show` in the same form:
//   Blend Mode = Opaque                      settings: Domain, Shading Model, Blend Mode, Two Sided, Max World Position Offset Displacement
//   Gold = ParameterColor #C9A227            name = Kind [value] [(Pin=source, ...)]
//   Grain = Noise "value false 8 1 2 2 0 1"
//   Mix = Lerp(a=#2E2E2E, b=Gold, t=Grain)
//   Output.BaseColor = Mix                   a link into the Material Output
// A source is a name (or name.pin), a literal (a number, #RRGGBB, "x y z", true) or a nested node: Lerp(#2E2E2E, Gold, Noise).
namespace graph_text {

// A node as the text names it: its name and kind, or just the kind when it has no name.
[[nodiscard]] std::string ShortLabel(const kb::render::RenderMaterialGraphNode& node) {
    const std::string kind{ kb::render::RenderMaterialGraphNodeKindName(node.kind) };
    return node.parameter.displayName.empty() ? kind : node.parameter.displayName + " (" + kind + ")";
}

using Kind = kb::render::RenderMaterialGraphNodeKind;

constexpr std::array<std::string_view, 5> kSettingKeys{ "domain", "shadingmodel", "blendmode", "twosided", "maxworldpositionoffsetdisplacement" };

[[nodiscard]] bool IsSettingKey(std::string_view normalized) {
    return std::ranges::find(kSettingKeys, normalized) != kSettingKeys.end();
}

// Nodes text cannot write or read back: they keep their own editors (material-function) and stay out of text.
[[nodiscard]] bool Representable(Kind kind) {
    switch (kind) {
    case Kind::MaterialFunctionCall:
    case Kind::CustomCode:
    case Kind::LayerStack:
    case Kind::FunctionInput:
    case Kind::FunctionOutput:
    case Kind::CompositeInput:
    case Kind::CompositeOutput:
        return false;
    default:
        return true;
    }
}

[[nodiscard]] bool IsLiteralConstant(Kind kind) {
    return kind == Kind::ConstantScalar || kind == Kind::ConstantVector || kind == Kind::ConstantColor;
}

// A node nobody named: its name is empty, or the one the editor gave it (Scalar, Fresnel). A parameter or a texture slot
// is always named, because an instance sets it by that name.
[[nodiscard]] bool IsAnonymousNode(const kb::render::RenderMaterialGraphNode& node) {
    if (kb::render::IsRenderMaterialGraphParameterNode(node.kind) || kb::render::IsRenderMaterialGraphTextureAssetNode(node.kind)) return false;
    return node.parameter.displayName.empty() || node.parameter.displayName == kb::render::RenderMaterialGraphConstantDisplayName(node.kind) ||
        node.parameter.displayName == kb::render::DefaultRenderMaterialGraphParameterMetadata(node.kind, node.id).displayName;
}

[[nodiscard]] std::string Identifier(std::string_view text) {
    std::string out;
    for (const char character : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' ? character : '_');
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) out.insert(out.begin(), 'n');
    return out;
}

[[nodiscard]] std::string Quoted(std::string_view value) {
    if (value.find_first_of(" \t(),\"=") == std::string_view::npos && !value.empty()) return std::string{ value };
    std::string text{ value };
    std::ranges::replace(text, '"', '\'');
    return "\"" + text + "\"";
}

[[nodiscard]] std::string HexColor(const float* linear) {
    char hex[16];
    const auto byte = [](float value) {
        return static_cast<unsigned>(std::clamp(kb::math::LinearToSrgb(std::clamp(value, 0.0F, 1.0F)) * 255.0F + 0.5F, 0.0F, 255.0F));
    };
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", byte(linear[0]), byte(linear[1]), byte(linear[2]));
    return hex;
}

// A colour as text: #RRGGBB when that gives the same colour back, else its linear numbers.
[[nodiscard]] std::string ColorText(const std::array<float, 4>& linear) {
    if (std::abs(linear[3] - 1.0F) < 1.0e-5F) {
        const std::string hex = HexColor(linear.data());
        if (const std::optional<std::array<float, 4>> back = ParseColorText(hex, 3);
            back.has_value() && std::abs((*back)[0] - linear[0]) < 1.0e-4F && std::abs((*back)[1] - linear[1]) < 1.0e-4F &&
            std::abs((*back)[2] - linear[2]) < 1.0e-4F) {
            return hex;
        }
    }
    return "\"" + kb::render::RenderMaterialGraphFloatText(linear[0]) + " " + kb::render::RenderMaterialGraphFloatText(linear[1]) + " " +
        kb::render::RenderMaterialGraphFloatText(linear[2]) + " " + kb::render::RenderMaterialGraphFloatText(linear[3]) + "\"";
}

[[nodiscard]] std::optional<std::array<float, 4>> Numbers(std::string_view text) {
    std::array<float, 4> values{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::istringstream stream{ std::string{ text } };
    std::size_t count = 0U;
    float value = 0.0F;
    while (stream >> value) {
        if (count == 4U || !std::isfinite(value)) return std::nullopt;
        values[count++] = value;
    }
    return count > 0U && stream.eof() ? std::optional{ values } : std::nullopt;
}

[[nodiscard]] std::string NodeValueText(const kb::render::RenderMaterialAssetData& asset, const kb::render::RenderMaterialGraphNode& node,
    const kb::assets::AssetRegistry& registry) {
    using kb::render::RenderMaterialParameterType;
    const auto stored = [&]() {
        return std::ranges::find_if(asset.graphParameterValues, [&](const kb::render::RenderMaterialGraphParameterValue& value) {
            return value.stableId == node.parameter.stableId;
        });
    };
    if (node.kind == Kind::ConstantColor) {
        if (const std::optional<std::array<float, 4>> color = Numbers(node.parameter.defaultValueHint)) return ColorText(*color);
    }
    if (kb::render::IsRenderMaterialGraphConstantNode(node.kind)) return Quoted(node.parameter.defaultValueHint);
    if (kb::render::IsRenderMaterialGraphParameterNode(node.kind) || kb::render::IsRenderMaterialGraphTextureAssetNode(node.kind)) {
        const auto value = stored();
        if (value == asset.graphParameterValues.end()) return {};
        switch (value->type) {
        case RenderMaterialParameterType::Scalar: return kb::render::RenderMaterialGraphFloatText(value->numbers[0]);
        case RenderMaterialParameterType::Vec3:
            return Quoted(kb::render::RenderMaterialGraphFloatText(value->numbers[0]) + " " + kb::render::RenderMaterialGraphFloatText(value->numbers[1]) + " " +
                kb::render::RenderMaterialGraphFloatText(value->numbers[2]));
        case RenderMaterialParameterType::Vec4:
        case RenderMaterialParameterType::Color: return ColorText(value->numbers);
        case RenderMaterialParameterType::Texture:
            return value->assetId == 0U ? std::string{} : Quoted(material::TexturePath(registry, value->assetId) + (value->numbers[0] >= 0.5F ? "@uv1" : ""));
        default: return {};
        }
    }
    // A hint the node got when it was made is not a setting.
    if (node.parameter.defaultValueHint.empty() ||
        node.parameter.defaultValueHint == kb::render::DefaultRenderMaterialGraphParameterMetadata(node.kind, node.id).defaultValueHint) {
        return {};
    }
    return Quoted(node.parameter.defaultValueHint);
}

[[nodiscard]] std::string Text(const kb::render::RenderMaterialAssetData& asset, const kb::assets::AssetRegistry& registry) {
    const kb::render::RenderMaterialGraphDocument& graph = asset.graph;
    std::ostringstream out;
    out << "Domain = " << kb::render::RenderMaterialDomainName(kb::render::ParseRenderMaterialDomain(graph.materialDomain)) << '\n'
        << "Shading Model = " << kb::render::RenderMaterialShadingModelName(kb::render::ParseRenderMaterialShadingModel(graph.shadingModel)) << '\n'
        << "Blend Mode = " << kb::render::RenderMaterialGraphBlendModeName(kb::render::ParseRenderMaterialGraphBlendMode(graph.blendMode)) << '\n';
    if (asset.desc.doubleSided) out << "Two Sided = true\n";
    if (graph.maxWorldPositionOffsetDisplacement > 0.0F) {
        out << "Max World Position Offset Displacement = " << kb::render::RenderMaterialGraphFloatText(graph.maxWorldPositionOffsetDisplacement) << '\n';
    }

    // Names: a node's own, or n<id>; unique and never a setting or Output.
    std::set<std::string> used{ "output", "true", "false" };
    for (const std::string_view key : kSettingKeys) used.emplace(key);
    std::map<std::uint32_t, std::string> names;
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == Kind::MaterialOutput) continue;
        std::string name = IsAnonymousNode(node) ? "n" + std::to_string(node.id) : Identifier(node.parameter.displayName);
        while (!used.insert(material::Normalized(name)).second) name += "_" + std::to_string(node.id);
        names.emplace(node.id, std::move(name));
    }
    std::map<std::uint32_t, std::size_t> uses;
    for (const kb::render::RenderMaterialGraphLink& link : graph.links) ++uses[link.fromNodeId];
    // A node nobody named, used once, is written where it is used: a number or colour as itself, any other as a nested node.
    const auto inlined = [&](const kb::render::RenderMaterialGraphNode& node) {
        if (node.kind == Kind::MaterialOutput || !Representable(node.kind) || uses[node.id] != 1U) return false;
        return IsAnonymousNode(node);
    };
    std::function<std::string(const kb::render::RenderMaterialGraphNode&)> expression;
    const auto source = [&](const kb::render::RenderMaterialGraphLink& link) {
        const kb::render::RenderMaterialGraphNode* from = kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId);
        if (from == nullptr) return std::string{ "?" };
        const std::vector<std::string> outputs = kb::render::RenderMaterialGraphNodeOutputPinNames(*from);
        const std::string pin = outputs.empty() || outputs.front() == link.fromPin ? std::string{} : "." + link.fromPin;
        if (inlined(*from)) return IsLiteralConstant(from->kind) ? NodeValueText(asset, *from, registry) : expression(*from) + pin;
        return names[from->id] + pin;
    };
    expression = [&](const kb::render::RenderMaterialGraphNode& node) {
        std::string out{ kb::render::RenderMaterialGraphNodeKindName(node.kind) };
        if (const std::string value = NodeValueText(asset, node, registry); !value.empty()) out += ' ' + value;
        const std::vector<std::string> pins = kb::render::RenderMaterialGraphNodeInputPinNames(node);
        std::string inputs;
        for (const std::string& pin : pins) {
            for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
                if (link.toNodeId == node.id && link.toPin == pin) inputs += (inputs.empty() ? "" : ", ") + (pins.size() == 1U ? std::string{} : pin + "=") + source(link);
            }
        }
        return inputs.empty() ? out : out + "(" + inputs + ")";
    };

    std::set<std::uint32_t> written;
    std::function<void(const kb::render::RenderMaterialGraphNode&)> write;
    // What a node needs before its own line: the named nodes below it, through the nested ones.
    const std::function<void(const kb::render::RenderMaterialGraphNode&)> dependencies = [&](const kb::render::RenderMaterialGraphNode& node) {
        for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
            const kb::render::RenderMaterialGraphNode* from = link.toNodeId == node.id ? kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId) : nullptr;
            if (from == nullptr) continue;
            if (inlined(*from)) {
                if (!IsLiteralConstant(from->kind)) dependencies(*from);
            } else {
                write(*from);
            }
        }
    };
    write = [&](const kb::render::RenderMaterialGraphNode& node) {
        if (node.kind == Kind::MaterialOutput || inlined(node) || !written.insert(node.id).second) return;
        dependencies(node);
        if (!Representable(node.kind)) {
            out << "// " << NodeLabel(node) << " (text cannot write this node; edit it with material-graph or material-function)\n";
            return;
        }
        out << names[node.id] << " = " << expression(node) << '\n';
    };
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) write(node);
    for (const kb::render::RenderMaterialGraphNode& output : graph.nodes) {
        if (output.kind != Kind::MaterialOutput) continue;
        dependencies(output);
        for (const std::string& pin : kb::render::RenderMaterialGraphNodeInputPinNames(output)) {
            for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
                if (link.toNodeId != output.id || link.toPin != pin) continue;
                const kb::render::RenderMaterialGraphNode* from = kb::render::FindRenderMaterialGraphNode(graph, link.fromNodeId);
                out << (from != nullptr && !Representable(from->kind) ? "// " : "") << "Output." << pin << " = " << source(link) << '\n';
            }
        }
    }
    return out.str();
}

// What the pins of a node take and give, for the messages that name a wrong pin.
[[nodiscard]] std::string PinList(const kb::render::RenderMaterialGraphNode& node, bool outputs) {
    std::string list;
    for (const std::string& pin : outputs ? kb::render::RenderMaterialGraphNodeOutputPinNames(node) : kb::render::RenderMaterialGraphNodeInputPinNames(node)) {
        list += (list.empty() ? "" : ", ") + pin + " (" +
            std::string{ kb::render::RenderMaterialGraphPinTypeName(kb::render::RenderMaterialGraphPinDataType(node, pin, outputs)) } + ")";
    }
    return list.empty() ? "none" : list;
}

[[nodiscard]] std::string Closest(std::string_view wanted, const std::vector<std::string>& candidates) {
    return kb::script::ClosestScriptName(wanted, std::span<const std::string>{ candidates });
}

[[nodiscard]] std::string DidYouMean(std::string_view wanted, const std::vector<std::string>& candidates) {
    const std::string closest = Closest(wanted, candidates);
    return closest.empty() ? std::string{} : "; did you mean " + closest + "?";
}

struct Source {
    std::optional<std::uint32_t> node;  // a node of the graph, or
    std::string literal;                // a literal: a number, #RRGGBB, quoted numbers, true or false
    std::string pin;                    // the output pin, empty for the first
};

class Parser {
public:
    Parser(kb::render::RenderMaterialAssetData& work, const material::TextureResolver& textures) : work_(work), textures_(textures) {}

    // The reason the text was refused (with its line), or empty.
    [[nodiscard]] std::string Run(std::string_view text) {
        std::size_t start = 0U;
        while (start <= text.size() && error_.empty()) {
            const std::size_t end = text.find('\n', start);
            ++line_;
            ParseLine(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
        if (error_.empty()) Layout();
        return error_;
    }

private:
    [[nodiscard]] bool Fail(const std::string& message) {
        if (error_.empty()) error_ = "line " + std::to_string(line_) + ": " + message;
        return false;
    }

    void SkipSpace() { while (pos_ < line_text_.size() && (line_text_[pos_] == ' ' || line_text_[pos_] == '\t' || line_text_[pos_] == '\r')) ++pos_; }
    [[nodiscard]] bool Peek(char character) { SkipSpace(); return pos_ < line_text_.size() && line_text_[pos_] == character; }
    [[nodiscard]] bool AtEnd() { SkipSpace(); return pos_ >= line_text_.size(); }

    [[nodiscard]] std::string Word() {
        SkipSpace();
        const std::size_t begin = pos_;
        while (pos_ < line_text_.size() && (std::isalnum(static_cast<unsigned char>(line_text_[pos_])) != 0 || line_text_[pos_] == '_')) ++pos_;
        return line_text_.substr(begin, pos_ - begin);
    }

    // "..." or a run of characters up to a space, bracket or comma.
    [[nodiscard]] std::optional<std::string> ValueToken(bool& quoted) {
        SkipSpace();
        quoted = false;
        if (pos_ >= line_text_.size()) return std::nullopt;
        if (line_text_[pos_] == '"') {
            const std::size_t close = line_text_.find('"', pos_ + 1U);
            if (close == std::string::npos) { static_cast<void>(Fail("a quoted value is missing its closing quote")); return std::nullopt; }
            const std::string text = line_text_.substr(pos_ + 1U, close - pos_ - 1U);
            pos_ = close + 1U;
            quoted = true;
            return text;
        }
        const std::size_t begin = pos_;
        while (pos_ < line_text_.size() && std::string_view{ " \t\r(),=" }.find(line_text_[pos_]) == std::string_view::npos) ++pos_;
        if (pos_ == begin) return std::nullopt;
        return line_text_.substr(begin, pos_ - begin);
    }

    [[nodiscard]] kb::render::RenderMaterialGraphNode* Mutable(std::uint32_t id) { return kb::render::FindMutableRenderMaterialGraphNode(work_.graph, id); }

    [[nodiscard]] std::string Label(std::uint32_t id) {
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(work_.graph, id);
        return node == nullptr ? std::string{ "?" } : ShortLabel(*node);
    }

    [[nodiscard]] const std::vector<std::string>& KnownNames() const { return defined_; }

    [[nodiscard]] std::vector<std::string> KindNames() const {
        std::vector<std::string> names;
        for (const Kind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
            if (kind != Kind::MaterialOutput && Representable(kind)) names.emplace_back(kb::render::RenderMaterialGraphNodeKindName(kind));
        }
        return names;
    }

    void ParseLine(std::string_view raw) {
        // A comment is // outside quotes, or a line that starts with "# ".
        std::string text;
        bool quoted = false;
        for (std::size_t index = 0U; index < raw.size(); ++index) {
            if (raw[index] == '"') quoted = !quoted;
            if (!quoted && raw[index] == '/' && index + 1U < raw.size() && raw[index + 1U] == '/') break;
            text.push_back(raw[index]);
        }
        const std::size_t first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos || (text[first] == '#' && (first + 1U == text.size() || text[first + 1U] == ' '))) return;
        const std::size_t equals = text.find('=');
        if (equals == std::string::npos) {
            static_cast<void>(Fail("expected 'name = Kind ...', 'Output.Pin = source' or 'Setting = value', got '" + text.substr(first) + "'"));
            return;
        }
        std::string left = text.substr(first, equals - first);
        while (!left.empty() && (left.back() == ' ' || left.back() == '\t')) left.pop_back();
        line_text_ = text.substr(equals + 1U);
        pos_ = 0U;
        if (IsSettingKey(material::Normalized(left))) {
            ApplySetting(left);
        } else if (material::Normalized(left).starts_with("output.")) {
            ParseOutputLink(left.substr(left.find('.') + 1U));
        } else {
            ParseNodeLine(left);
        }
    }

    void ApplySetting(const std::string& key) {
        std::string value = line_text_;
        value.erase(0U, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.pop_back();
        if (value.size() > 1U && value.front() == '"' && value.back() == '"') value = value.substr(1U, value.size() - 2U);
        if (material::Normalized(key) == "twosided") {
            bool known = false;
            if (const std::string problem = material::ApplyBuiltinField(work_.desc, "doubleSided", value, textures_, known); !problem.empty()) {
                static_cast<void>(Fail("Two Sided: " + problem + " (true or false)"));
            }
            return;
        }
        if (const std::string problem = SetGraphSetting(work_, key, value); !problem.empty()) static_cast<void>(Fail(problem));
    }

    void ParseOutputLink(const std::string& pin) {
        const std::uint32_t output = OutputId();
        Source from;
        if (!ParseSource(from)) return;
        if (!AtEnd()) { static_cast<void>(Fail("unexpected text after the source: '" + line_text_.substr(pos_) + "'")); return; }
        static_cast<void>(Connect(from, output, pin));
    }

    [[nodiscard]] std::uint32_t OutputId() {
        for (const kb::render::RenderMaterialGraphNode& node : work_.graph.nodes) {
            if (node.kind == Kind::MaterialOutput) return node.id;
        }
        return 0U;
    }

    void ParseNodeLine(const std::string& left) {
        const std::string name = left;
        if (name.empty() || Identifier(name) != name) {
            static_cast<void>(Fail("'" + name + "' is not a node name (letters, digits and _, not starting with a digit)"));
            return;
        }
        const std::string normalized = material::Normalized(name);
        if (names_.contains(normalized) || normalized == "output" || normalized == "true" || normalized == "false") {
            static_cast<void>(Fail("the name '" + name + "' is already used or reserved; give each node its own name"));
            return;
        }
        std::uint32_t id = 0U;
        if (!ParseNode(name, id)) return;
        if (!AtEnd()) static_cast<void>(Fail("unexpected text after the node: '" + line_text_.substr(pos_) + "'"));
    }

    // Kind [value] [(inputs)] at the cursor: the node is made (named when `name` is not empty) and its inputs linked.
    [[nodiscard]] bool ParseNode(const std::string& name, std::uint32_t& id) {
        const std::string kindText = Word();
        const std::optional<Kind> kind = kindText.empty() ? std::nullopt : ParseKind(kindText);
        if (!kind.has_value()) {
            return Fail("unknown node kind '" + kindText + "'" + DidYouMean(kindText, KindNames()) + " (material-graph nodes lists the kinds)");
        }
        if (*kind == Kind::MaterialOutput || !Representable(*kind)) {
            return Fail(std::string{ kb::render::RenderMaterialGraphNodeKindName(*kind) } + " cannot be written in graph text; the Material Output is written as Output.Pin = source");
        }
        const std::optional<std::uint32_t> added = kb::render::AddRenderMaterialGraphNode(work_.graph, *kind, 0, 0);
        if (!added.has_value()) return Fail("a " + std::string{ kb::render::RenderMaterialGraphNodeKindName(*kind) } + " node cannot be added");
        id = *added;
        if (!name.empty()) {
            names_.emplace(material::Normalized(name), id);
            defined_.push_back(name);
            static_cast<void>(kb::render::RenameRenderMaterialGraphNode(work_.graph, id, name, kb::render::RenderMaterialGraphNodeKindName(*kind)));
            // A parameter keeps its name as its id, so instances and a rewritten text still find it.
            if (kb::render::IsRenderMaterialGraphParameterNode(*kind) || kb::render::IsRenderMaterialGraphTextureAssetNode(*kind)) {
                Mutable(id)->parameter.stableId = material::Normalized(name);
            }
        }
        // The value, unless the node ends here or the input list starts.
        SkipSpace();
        const bool endsHere = pos_ >= line_text_.size() || std::string_view{ "(),=" }.find(line_text_[pos_]) != std::string_view::npos ||
            (line_text_[pos_] == '.' && (pos_ + 1U >= line_text_.size() || std::isdigit(static_cast<unsigned char>(line_text_[pos_ + 1U])) == 0));
        if (!endsHere) {
            bool quoted = false;
            const std::optional<std::string> value = ValueToken(quoted);
            if (!value.has_value()) return error_.empty() ? Fail("expected a value or '(' after " + std::string{ kindText }) : false;
            const kb::render::RenderMaterialGraphNode node = *Mutable(id);
            const kb::render::RenderMaterialGraphNodeDoc* doc = kb::render::FindRenderMaterialGraphNodeDoc(*kind);
            const bool takesValue = kb::render::IsRenderMaterialGraphConstantNode(*kind) || kb::render::IsRenderMaterialGraphParameterNode(*kind) ||
                kb::render::IsRenderMaterialGraphTextureAssetNode(*kind) || (doc != nullptr && !doc->value.empty());
            if (!takesValue) return Fail(ShortLabel(node) + " takes no value; its inputs go in brackets: " + std::string{ kindText } + "(...)");
            if (const std::string problem = SetNodeValue(work_, node, *value, textures_); !problem.empty()) {
                return Fail(problem + (doc != nullptr && !doc->value.empty() ? " (the value of " + std::string{ kindText } + " is: " + std::string{ doc->value } + ")" : std::string{}));
            }
            if (!name.empty() && Mutable(id) != nullptr) {
                // Setting a texture or a constant may name the node itself; the text's name wins.
                static_cast<void>(kb::render::RenameRenderMaterialGraphNode(work_.graph, id, name, kb::render::RenderMaterialGraphNodeKindName(*kind)));
            }
        }
        if (Peek('(')) return ParseInputs(id);
        return true;
    }

    [[nodiscard]] bool ParseInputs(std::uint32_t id) {
        ++pos_;  // (
        std::size_t positional = 0U;
        while (true) {
            if (Peek(')')) { ++pos_; return true; }
            if (AtEnd()) return Fail("the input list of " + Label(id) + " is missing its closing )");
            // "pin=source" or a source for the next input.
            std::string pin;
            const std::size_t mark = pos_;
            const std::string word = Word();
            if (!word.empty() && Peek('=')) {
                ++pos_;
                pin = word;
            } else {
                pos_ = mark;
                const std::vector<std::string> inputs = kb::render::RenderMaterialGraphNodeInputPinNames(*Mutable(id));
                if (positional >= inputs.size()) return Fail(Label(id) + " has only " + std::to_string(inputs.size()) + " inputs: " + PinList(*Mutable(id), false));
                pin = inputs[positional];
            }
            ++positional;
            Source from;
            if (!ParseSource(from) || !Connect(from, id, pin)) return false;
            if (Peek(',')) { ++pos_; continue; }
            if (Peek(')')) { ++pos_; return true; }
            return Fail("expected , or ) in the inputs of " + Label(id) + ", got '" + line_text_.substr(pos_) + "'");
        }
    }

    [[nodiscard]] bool ParseSource(Source& source) {
        SkipSpace();
        if (pos_ >= line_text_.size()) return Fail("a source is missing after =");
        const char first = line_text_[pos_];
        if (first == '"' || first == '#' || first == '-' || first == '.' || std::isdigit(static_cast<unsigned char>(first)) != 0) {
            bool quoted = false;
            const std::optional<std::string> token = ValueToken(quoted);
            if (!token.has_value()) return false;
            source.literal = quoted ? "\"" + *token : *token;
            return true;
        }
        const std::size_t mark = pos_;
        const std::string word = Word();
        if (word.empty()) return Fail("expected a name, a node kind or a value, got '" + line_text_.substr(pos_) + "'");
        if (const auto known = names_.find(material::Normalized(word)); known != names_.end()) {
            source.node = known->second;
            if (pos_ < line_text_.size() && line_text_[pos_] == '.') { ++pos_; source.pin = Word(); }
            return true;
        }
        if (word == "true" || word == "false") { source.literal = word; return true; }
        if (ParseKind(word).has_value()) {
            pos_ = mark;
            std::uint32_t id = 0U;
            if (!ParseNode({}, id)) return false;
            source.node = id;
            if (pos_ < line_text_.size() && line_text_[pos_] == '.') { ++pos_; source.pin = Word(); }
            return true;
        }
        std::vector<std::string> candidates = defined_;
        const std::vector<std::string> kinds = KindNames();
        candidates.insert(candidates.end(), kinds.begin(), kinds.end());
        return Fail("'" + word + "' is neither a node defined above nor a node kind" + DidYouMean(word, candidates) +
            (names_.empty() ? std::string{} : " (defined: " + JoinNames(KnownNames()) + ")"));
    }

    struct LiteralChoice {
        Kind kind;
        std::string value;
        std::string pin;
    };

    [[nodiscard]] std::vector<LiteralChoice> LiteralChoices(const std::string& literal) {
        using std::string;
        if (literal == "true" || literal == "false") return { { Kind::ConstantBool, literal, "value" } };
        const std::string inner = literal.front() == '"' ? literal.substr(1U) : literal;
        if (!inner.empty() && inner.front() == '#') {
            const std::optional<std::array<float, 4>> color = ParseColorText(inner, 4);
            if (!color.has_value()) return {};
            const auto f = [](float value) { return kb::render::RenderMaterialGraphFloatText(value); };
            return { { Kind::ConstantColor, f((*color)[0]) + " " + f((*color)[1]) + " " + f((*color)[2]) + " " + f((*color)[3]), "rgba" },
                { Kind::ConstantVector, f((*color)[0]) + " " + f((*color)[1]) + " " + f((*color)[2]), "xyz" } };
        }
        const std::string& numbers = inner;
        const std::optional<std::array<float, 4>> values = Numbers(numbers);
        if (!values.has_value()) return {};
        const std::size_t count = static_cast<std::size_t>(std::ranges::count(numbers, ' ')) + 1U;
        const auto f = [&](std::size_t index) { return kb::render::RenderMaterialGraphFloatText((*values)[index]); };
        switch (count) {
        case 1U: return { { Kind::ConstantScalar, f(0U), "value" }, { Kind::ConstantColor, f(0U) + " " + f(0U) + " " + f(0U) + " 1", "rgba" },
            { Kind::ConstantVector, f(0U) + " " + f(0U) + " " + f(0U), "xyz" } };
        case 2U: return { { Kind::ConstantVector2, f(0U) + " " + f(1U), "xy" } };
        case 3U: return { { Kind::ConstantVector, f(0U) + " " + f(1U) + " " + f(2U), "xyz" }, { Kind::ConstantColor, f(0U) + " " + f(1U) + " " + f(2U) + " 1", "rgba" } };
        default: return { { Kind::ConstantColor, f(0U) + " " + f(1U) + " " + f(2U) + " " + f(3U), "rgba" } };
        }
    }

    [[nodiscard]] bool Connect(const Source& from, std::uint32_t toId, const std::string& toPinText) {
        const kb::render::RenderMaterialGraphNode* to = kb::render::FindRenderMaterialGraphNode(work_.graph, toId);
        if (to == nullptr) return Fail("internal: the target node is missing");
        const std::vector<std::string> inputs = kb::render::RenderMaterialGraphNodeInputPinNames(*to);
        const auto inputPin = std::ranges::find_if(inputs, [&](const std::string& pin) { return material::Normalized(pin) == material::Normalized(toPinText); });
        const std::string target = to->kind == Kind::MaterialOutput ? "Output" : ShortLabel(*to);
        if (inputPin == inputs.end()) {
            return Fail(target + " has no input '" + toPinText + "'" + DidYouMean(toPinText, inputs) + "; inputs: " + PinList(*to, false));
        }
        const std::string toPin = *inputPin;
        if (!assigned_.insert({ toId, toPin }).second) return Fail(target + " input " + toPin + " is set twice");

        std::uint32_t fromId = 0U;
        std::string fromPin;
        if (from.node.has_value()) {
            fromId = *from.node;
            const kb::render::RenderMaterialGraphNode* source = kb::render::FindRenderMaterialGraphNode(work_.graph, fromId);
            const std::vector<std::string> outputs = kb::render::RenderMaterialGraphNodeOutputPinNames(*source);
            if (outputs.empty()) return Fail(ShortLabel(*source) + " has no output to connect");
            fromPin = outputs.front();
            if (!from.pin.empty()) {
                const auto found = std::ranges::find_if(outputs, [&](const std::string& pin) { return material::Normalized(pin) == material::Normalized(from.pin); });
                if (found == outputs.end()) {
                    return Fail(ShortLabel(*source) + " has no output '" + from.pin + "'" + DidYouMean(from.pin, outputs) + "; outputs: " + PinList(*source, true));
                }
                fromPin = *found;
            }
            if (!kb::render::ConnectRenderMaterialGraphPins(work_.graph, fromId, fromPin, toId, toPin)) {
                const std::string wantedType{ kb::render::RenderMaterialGraphPinTypeName(kb::render::RenderMaterialGraphPinDataType(*to, toPin, false)) };
                const std::string givenType{ kb::render::RenderMaterialGraphPinTypeName(kb::render::RenderMaterialGraphPinDataType(*source, fromPin, true)) };
                // A float is no colour or vector, but a node that gives float4 (Multiply, Lerp) takes it in.
                const bool widen = givenType == "float" && (wantedType == "color" || wantedType == "float3" || wantedType == "float2" || wantedType == "normal");
                return Fail(ShortLabel(*source) + " output " + fromPin + " (" + givenType + ") cannot connect to " + target + " input " + toPin + " (" + wantedType +
                    "); outputs of " + ShortLabel(*source) + ": " + PinList(*source, true) +
                    (widen ? "; a float becomes a colour through a node that gives float4, for example Multiply(" + ShortLabel(*source) + ", #FFFFFF)" : std::string{}));
            }
            edges_.emplace_back(fromId, toId);
            return true;
        }
        for (const LiteralChoice& choice : LiteralChoices(from.literal)) {
            if (!kb::render::AreRenderMaterialGraphPinsCompatible(choice.kind, choice.pin, to->kind, toPin)) continue;
            const std::optional<std::uint32_t> constant = kb::render::AddRenderMaterialGraphNode(work_.graph, choice.kind, 0, 0);
            if (!constant.has_value() || !kb::render::SetRenderMaterialGraphConstantValue(work_.graph, *constant, choice.value) ||
                !kb::render::ConnectRenderMaterialGraphPins(work_.graph, *constant, choice.pin, toId, toPin)) {
                return Fail("'" + from.literal + "' is not a value " + target + " input " + toPin + " takes");
            }
            edges_.emplace_back(*constant, toId);
            return true;
        }
        const kb::render::RenderMaterialGraphNode* again = kb::render::FindRenderMaterialGraphNode(work_.graph, toId);
        return Fail("the value " + from.literal + " does not fit " + target + " input " + toPin + " (" +
            std::string{ kb::render::RenderMaterialGraphPinTypeName(kb::render::RenderMaterialGraphPinDataType(*again, toPin, false)) } +
            "): a number fits a float, #RRGGBB a colour or xyz, \"x y z\" a vector");
    }

    // Left to right by depth, so the graph reads in the editor as it does in text.
    void Layout() {
        std::map<std::uint32_t, int> depth;
        for (const kb::render::RenderMaterialGraphNode& node : work_.graph.nodes) depth[node.id] = 0;
        for (std::size_t round = 0U; round < work_.graph.nodes.size(); ++round) {
            bool changed = false;
            for (const auto& [from, to] : edges_) {
                if (depth[to] < depth[from] + 1) { depth[to] = depth[from] + 1; changed = true; }
            }
            if (!changed) break;
        }
        std::map<int, int> rows;
        for (kb::render::RenderMaterialGraphNode& node : work_.graph.nodes) {
            const int column = node.kind == Kind::MaterialOutput ? 0 : depth[node.id];
            node.positionX = (node.kind == Kind::MaterialOutput ? static_cast<int>(depth.size()) : column) * 240;
            node.positionY = (node.kind == Kind::MaterialOutput ? 0 : rows[column]++) * 110;
        }
    }

    kb::render::RenderMaterialAssetData& work_;
    const material::TextureResolver& textures_;
    std::string error_;
    std::size_t line_ = 0U;
    std::string line_text_;
    std::size_t pos_ = 0U;
    std::map<std::string, std::uint32_t> names_;
    std::vector<std::string> defined_;
    std::set<std::pair<std::uint32_t, std::string>> assigned_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges_;
};

// The graph of `asset` replaced by the graph `text` describes; the reason it was refused, or empty (the asset is then unchanged).
[[nodiscard]] std::string Apply(kb::render::RenderMaterialAssetData& asset, std::string_view text, const material::TextureResolver& textures,
    const kb::assets::AssetRegistry& registry) {
    for (const kb::render::RenderMaterialGraphNode& node : asset.graph.nodes) {
        if (!Representable(node.kind)) {
            return ShortLabel(node) + " cannot be written in graph text, so the text would delete it; change this graph with material-graph add-node, connect and set";
        }
    }
    kb::render::RenderMaterialAssetData work = asset;
    work.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    work.graphParameterValues.clear();
    work.desc.doubleSided = false;
    Parser parser{ work, textures };
    if (std::string problem = parser.Run(text); !problem.empty()) return problem;
    const MaterialCheck check = CheckMaterial(work, registry);
    if (!check.problems.empty()) {
        std::string problems = "the graph does not compile:";
        for (const std::string& problem : check.problems) problems += "\n  " + problem;
        return problems;
    }
    asset = std::move(work);
    return {};
}

} // namespace graph_text

// What the graph costs per pixel, and why a phone cannot take it.
kb::render::RenderMaterialGraphCost PrintCost(const kb::render::RenderMaterialAssetData& asset, CommandIo io) {
    const kb::render::RenderMaterialGraphCost cost = kb::render::EstimateRenderMaterialGraphCost(asset.graph);
    io.out << "Cost: about " << cost.instructions << " instructions, " << cost.textureSamples << " texture samples, " << cost.nodes << " nodes\n";
    for (const std::string& warning : cost.mobileWarnings) io.out << "Warning: too heavy for a phone: " << warning << '\n';
    for (const std::string& note : cost.notes) io.out << "Note: " << note << '\n';
    return cost;
}

// show: the graph as text, whether it compiles, what it costs and the parameters an instance can set.
void PrintGraph(const kb::render::RenderMaterialAssetData& asset, const std::string& virtualPath,
    const kb::assets::AssetRegistry& registry, CommandIo io) {
    const std::string text = graph_text::Text(asset, registry);
    io.out << virtualPath << '\n' << text;
    const std::vector<ExposedParameter> parameters = ExposedParameters(asset);
    if (!parameters.empty()) io.out << "Parameters: " << ParameterList(parameters) << '\n';
    const MaterialCheck check = CheckMaterial(asset, registry);
    io.out << (check.problems.empty() ? "Compiles: yes\n" : "Compiles: no\n");
    for (const std::string& problem : check.problems) io.out << "  " << problem << '\n';
    const kb::render::RenderMaterialGraphCost cost = PrintCost(asset, io);
    if (io.WantsData()) {
        kb::core::JsonValue data = DataObject("21kb.cli.material-graph-show/v1");
        data.Set("material", kb::core::JsonValue::MakeString(virtualPath));
        data.Set("text", kb::core::JsonValue::MakeString(ToValidUtf8(text)));
        data.Set("compiles", kb::core::JsonValue::MakeBool(check.problems.empty()));
        data.Set("instructions", kb::core::JsonValue::MakeNumber(static_cast<double>(cost.instructions)));
        data.Set("textureSamples", kb::core::JsonValue::MakeNumber(static_cast<double>(cost.textureSamples)));
        data.Set("heavyForPhone", kb::core::JsonValue::MakeBool(!cost.mobileWarnings.empty()));
        PublishData(io, std::move(data));
    }
}

// ---- Node reference: the registry's pins and types with the words kept beside it (RenderMaterialGraphNodeDocs) ---------

// A line of graph text that uses the node: what an agent adapts.
[[nodiscard]] std::string NodeExample(kb::render::RenderMaterialGraphNodeKind kind) {
    using Pin = kb::render::RenderMaterialGraphPinType;
    using K = kb::render::RenderMaterialGraphNodeKind;
    const std::string name{ kb::render::RenderMaterialGraphNodeKindName(kind) };
    switch (kind) {
    case K::ConstantScalar: return "Half = ConstantScalar 0.5";
    case K::ConstantVector2: return "Pair = ConstantVector2 \"2 2\"";
    case K::ConstantVector: return "Up = ConstantVector \"0 0 1\"";
    case K::ConstantColor: return "Gold = ConstantColor #C9A227";
    case K::ConstantBool: return "On = ConstantBool true";
    case K::ParameterScalar: return "Shine = ParameterScalar 0.5";
    case K::ParameterVector: return "Direction = ParameterVector \"0 1 0\"";
    case K::ParameterColor: return "Tint = ParameterColor #C9A227";
    case K::ParameterTexture: return "Albedo = ParameterTexture none";
    default: break;
    }
    kb::render::RenderMaterialGraphNode node;
    node.kind = kind;
    std::string inputs;
    std::size_t shown = 0U;
    for (const std::string& pin : kb::render::RenderMaterialGraphNodeInputPinNames(node)) {
        if (shown == 3U) break;
        std::string value;
        switch (kb::render::RenderMaterialGraphPinDataType(node, pin, false)) {
        case Pin::Float: value = "0.5"; break;
        case Pin::Float2: value = "\"1 1\""; break;
        case Pin::Float3:
        case Pin::Normal: value = "\"0 0 1\""; break;
        case Pin::Float4:
        case Pin::Color: value = "#808080"; break;
        case Pin::Bool: value = "true"; break;
        default: break;
        }
        if (value.empty()) continue;
        inputs += (inputs.empty() ? "" : ", ") + pin + "=" + value;
        ++shown;
    }
    return "Result = " + name + (inputs.empty() ? std::string{} : "(" + inputs + ")");
}

void PrintNodePage(const kb::render::RenderMaterialGraphNodeKind kind, CommandIo io) {
    const kb::render::RenderMaterialGraphNodeDoc* doc = kb::render::FindRenderMaterialGraphNodeDoc(kind);
    kb::render::RenderMaterialGraphNode node;
    node.kind = kind;
    io.out << kb::render::RenderMaterialGraphNodeKindName(kind) << " (" << (doc != nullptr ? doc->group : std::string_view{ "?" }) << ")\n"
           << (doc != nullptr ? doc->purpose : std::string_view{}) << '\n'
           << "Inputs: " << graph_text::PinList(node, false) << '\n'
           << "Outputs: " << graph_text::PinList(node, true) << '\n';
    if (doc != nullptr && !doc->value.empty()) io.out << "Value: " << doc->value << '\n';
    io.out << "Example: " << NodeExample(kind) << '\n';
    if (kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
        io.out << "Write a link as: Output.BaseColor = source\n";
    }
}

// The kinds a text names, grouped; `group` keeps one group (any case), empty keeps all. False when the group is unknown.
[[nodiscard]] bool PrintNodeIndex(std::string_view group, CommandIo io) {
    std::vector<std::string> groups;
    for (const kb::render::RenderMaterialGraphNodeDoc& doc : kb::render::RenderMaterialGraphNodeDocs()) {
        if (std::ranges::find(groups, std::string{ doc.group }) == groups.end()) groups.emplace_back(doc.group);
    }
    if (!group.empty() && std::ranges::none_of(groups, [&](const std::string& name) { return material::Normalized(name) == material::Normalized(group); })) {
        io.err << "error: there is no node group '" << group << "'; groups: " << JoinNames(groups) << '\n';
        return false;
    }
    for (const std::string& name : groups) {
        if (!group.empty() && material::Normalized(name) != material::Normalized(group)) continue;
        io.out << name << ":\n";
        for (const kb::render::RenderMaterialGraphNodeDoc& doc : kb::render::RenderMaterialGraphNodeDocs()) {
            if (doc.group != name) continue;
            std::string purpose{ doc.purpose };
            if (const std::size_t stop = purpose.find(". "); stop != std::string::npos) purpose.resize(stop);
            io.out << "  " << kb::render::RenderMaterialGraphNodeKindName(doc.kind) << " - " << purpose << '\n';
        }
    }
    io.out << "One node in full (pins, value, example): material-graph node --kind <Kind>\n";
    return true;
}

int GraphCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    if (action == "nodes") {
        return PrintNodeIndex(arguments.Option("--group").value_or(std::string{}), io) ? 0 : 1;
    }
    if (action == "node") {
        const std::optional<std::string> kindText = arguments.Option("--kind");
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = kindText.has_value() ? ParseKind(*kindText) : std::nullopt;
        if (!kind.has_value()) {
            std::vector<std::string> names;
            for (const kb::render::RenderMaterialGraphNodeKind candidate : kb::render::AllRenderMaterialGraphNodeKinds()) {
                names.emplace_back(kb::render::RenderMaterialGraphNodeKindName(candidate));
            }
            io.err << "error: material-graph node requires --kind <Kind>" << (kindText.has_value() ? ", not '" + *kindText + "'" + graph_text::DidYouMean(*kindText, names) : std::string{})
                   << " (material-graph nodes lists them)\n";
            return 1;
        }
        PrintNodePage(*kind, io);
        return 0;
    }
    LoadedProject project{ .root = material::ProjectRoot(arguments, "material-graph", io) };
    const std::optional<std::string> materialText = arguments.Option("--material");
    if (project.root.empty() || !materialText.has_value()) {
        if (!project.root.empty()) io.err << "error: material-graph " << action << " requires --project <dir> --material /Game/<path>.kbmat\n";
        return 1;
    }
    const std::optional<material::ProjectAsset> located = material::ProjectAssetFile(
        project.root, *materialText, kMaterialExtension, "material", "create one with kb_cli material-new", io);
    if (!located.has_value() || !Mount(project, io)) return 1;
    std::optional<kb::render::RenderMaterialAssetData> asset = kb::render::RenderMaterialAssetLoader::LoadMaterial(located->file);
    if (!asset.has_value()) {
        io.err << "error: the material could not be read: " << located->virtualPath << '\n';
        return 1;
    }
    const kb::assets::AssetRegistry& registry = Registry(project);
    const material::TextureResolver textures = material::ProjectTextureResolver(registry);
    kb::render::EnsureEditableRenderMaterialGraph(asset->graph);

    const auto node = [&](const char* option) -> const kb::render::RenderMaterialGraphNode* {
        const std::optional<std::string> reference = arguments.Option(option);
        if (!reference.has_value()) {
            io.err << "error: material-graph " << action << " requires " << option << " <node id | name | Output>\n";
            return nullptr;
        }
        const kb::render::RenderMaterialGraphNode* found = FindNode(asset->graph, *reference);
        if (found == nullptr) {
            io.err << "error: the graph has no node '" << *reference << "' (kb_cli material-graph show lists them)\n";
        }
        return found;
    };

    if (action == "show") {
        PrintGraph(*asset, located->virtualPath, registry, io);
        return 0;
    }
    if (action == "text") {
        const std::optional<std::string> text = arguments.Option("--text");
        if (!text.has_value()) {
            io.err << "error: material-graph text requires --text \"<graph text>\" (show prints the current graph in that form; material-graph node --kind <Kind> shows a node)\n";
            return 1;
        }
        if (const std::string problem = graph_text::Apply(*asset, *text, textures, registry); !problem.empty()) {
            io.err << "error: " << problem << "\nnothing was changed\n";
            return 1;
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "set the graph of " << located->virtualPath << ": " << asset->graph.nodes.size() << " nodes\n";
        if (const std::vector<ExposedParameter> parameters = ExposedParameters(*asset); !parameters.empty()) io.out << "Parameters: " << ParameterList(parameters) << '\n';
        PrintCost(*asset, io);
        return 0;
    }
    if (action == "add-node") {
        const std::optional<std::string> kindText = arguments.Option("--kind");
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = kindText.has_value() ? ParseKind(*kindText) : std::nullopt;
        if (!kind.has_value()) {
            io.err << "error: material-graph add-node requires --kind <node kind> (e.g. ScalarParameter, VectorParameter, "
                      "TextureParameter, Constant, Multiply; kb_cli material-graph nodes lists them)\n";
            return 1;
        }
        std::int32_t x = 0;
        std::int32_t y = static_cast<std::int32_t>(asset->graph.nodes.size()) * 120;
        if (const std::optional<std::string> at = arguments.Option("--at")) {
            const std::size_t comma = at->find(',');
            if (comma == std::string::npos ||
                std::from_chars(at->data(), at->data() + comma, x).ec != std::errc{} ||
                std::from_chars(at->data() + comma + 1U, at->data() + at->size(), y).ec != std::errc{}) {
                io.err << "error: --at takes x,y (graph units), got '" << *at << "'\n";
                return 1;
            }
        }
        const std::optional<std::uint32_t> id = kb::render::AddRenderMaterialGraphNode(asset->graph, *kind, x, y);
        if (!id.has_value()) {
            io.err << "error: a " << kb::render::RenderMaterialGraphNodeKindName(*kind) << " node cannot be added\n";
            return 1;
        }
        if (const std::optional<std::string> name = arguments.Option("--name")) {
            static_cast<void>(kb::render::RenameRenderMaterialGraphNode(asset->graph, *id, *name,
                kb::render::RenderMaterialGraphNodeKindName(*kind)));
        }
        if (const std::optional<std::string> value = arguments.Option("--value")) {
            const kb::render::RenderMaterialGraphNode added = *kb::render::FindRenderMaterialGraphNode(asset->graph, *id);
            if (const std::string problem = SetNodeValue(*asset, added, *value, textures); !problem.empty()) {
                io.err << "error: " << problem << '\n';
                return 1;
            }
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "added node " << NodeLabel(*kb::render::FindRenderMaterialGraphNode(asset->graph, *id)) << '\n';
        return 0;
    }
    if (action == "connect") {
        const kb::render::RenderMaterialGraphNode* from = node("--from");
        if (from == nullptr) return 1;
        const kb::render::RenderMaterialGraphNode* to = node("--to");
        if (to == nullptr) return 1;
        const std::vector<std::string> outputs = kb::render::RenderMaterialGraphNodeOutputPinNames(*from);
        const std::vector<std::string> inputs = kb::render::RenderMaterialGraphNodeInputPinNames(*to);
        // Pins by their name in any case and spacing (BaseColor, base_color, baseColor).
        const auto pin = [](const std::vector<std::string>& pins, const std::string& wanted) {
            const auto found = std::ranges::find_if(pins, [&](const std::string& candidate) {
                return material::Normalized(candidate) == material::Normalized(wanted);
            });
            return found != pins.end() ? *found : wanted;
        };
        const std::string fromPin = arguments.Option("--from-pin").has_value()
            ? pin(outputs, *arguments.Option("--from-pin")) : (outputs.empty() ? std::string{} : outputs.front());
        std::optional<std::string> toPin = arguments.Option("--to-pin");
        if (toPin.has_value()) toPin = pin(inputs, *toPin);
        if (!toPin.has_value()) {
            io.err << "error: material-graph connect requires --to-pin; node " << NodeLabel(*to) << " inputs: " << JoinNames(inputs) << '\n';
            return 1;
        }
        const std::uint32_t fromId = from->id;
        const std::uint32_t toId = to->id;
        const std::string fromLabel = NodeLabel(*from);
        const std::string toLabel = NodeLabel(*to);
        if (!kb::render::ConnectRenderMaterialGraphPins(asset->graph, fromId, fromPin, toId, *toPin)) {
            io.err << "error: " << fromLabel << " output '" << fromPin << "' cannot connect to " << toLabel << " input '" << *toPin
                   << "' (outputs: " << JoinNames(outputs) << "; inputs: " << JoinNames(inputs) << ")\n";
            return 1;
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "connected " << fromId << '.' << fromPin << " -> " << toId << '.' << *toPin << '\n';
        return 0;
    }
    if (action == "set") {
        const kb::render::RenderMaterialGraphNode* target = node("--node");
        const std::optional<std::string> value = arguments.Option("--value");
        if (target == nullptr || !value.has_value()) {
            if (target != nullptr) io.err << "error: material-graph set requires --value <value>\n";
            return 1;
        }
        const kb::render::RenderMaterialGraphNode copy = *target;
        if (const std::string problem = SetNodeValue(*asset, copy, *value, textures); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "set " << NodeLabel(*kb::render::FindRenderMaterialGraphNode(asset->graph, copy.id)) << " = " << *value << '\n';
        return 0;
    }
    if (action == "promote") {
        const kb::render::RenderMaterialGraphNode* target = node("--node");
        if (target == nullptr) return 1;
        const std::uint32_t id = target->id;
        const std::string label = NodeLabel(*target);
        if (!kb::render::PromoteRenderMaterialGraphNodeToParameter(asset->graph, id)) {
            io.err << "error: " << label << " cannot become a parameter (Scalar, XY, RGB and RGBA constants can)\n";
            return 1;
        }
        if (const std::optional<std::string> name = arguments.Option("--name")) {
            const kb::render::RenderMaterialGraphNode* promoted = kb::render::FindRenderMaterialGraphNode(asset->graph, id);
            static_cast<void>(kb::render::RenameRenderMaterialGraphNode(asset->graph, id, *name,
                kb::render::RenderMaterialGraphNodeKindName(promoted->kind)));
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "promoted " << NodeLabel(*kb::render::FindRenderMaterialGraphNode(asset->graph, id)) << '\n';
        return 0;
    }
    if (action == "rename") {
        const kb::render::RenderMaterialGraphNode* target = node("--node");
        const std::optional<std::string> name = arguments.Option("--name");
        if (target == nullptr || !name.has_value()) {
            if (target != nullptr) io.err << "error: material-graph rename requires --name <Name>\n";
            return 1;
        }
        const std::uint32_t id = target->id;
        static_cast<void>(kb::render::RenameRenderMaterialGraphNode(asset->graph, id, *name,
            kb::render::RenderMaterialGraphNodeKindName(target->kind)));
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "renamed " << NodeLabel(*kb::render::FindRenderMaterialGraphNode(asset->graph, id)) << '\n';
        return 0;
    }
    if (action == "setting") {
        const std::optional<std::string> name = arguments.Option("--name");
        const std::optional<std::string> value = arguments.Option("--value");
        if (!name.has_value() || !value.has_value()) {
            io.err << "error: material-graph setting requires --name <Domain | Shading Model | Blend Mode> --value <value>\n";
            return 1;
        }
        if (const std::string problem = SetGraphSetting(*asset, *name, *value); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
        if (!SaveMaterial(*asset, *located, registry, io)) return 1;
        io.out << "set " << *name << " = " << *value << '\n';
        return 0;
    }
    io.err << "error: material-graph expects show, kinds, add-node, connect, set, setting, promote or rename\n";
    return 1;
}

// The parent material of an instance (a .kbmat of the project), read from its file.
[[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> ReadParent(
    const LoadedProject& project, kb::assets::AssetId parentId, std::string& parentPath, CommandIo io) {
    const kb::assets::AssetMetadata* metadata = Registry(project).Find(parentId);
    if (metadata == nullptr || metadata->type != "RenderMaterial") {
        io.err << "error: the instance's parent material " << parentId.value << " is not a material of the project\n";
        return std::nullopt;
    }
    parentPath = material::Utf8Text(metadata->virtualPath);
    std::optional<kb::render::RenderMaterialAssetData> parent = kb::render::RenderMaterialAssetLoader::LoadMaterial(metadata->physicalPath);
    if (!parent.has_value()) {
        io.err << "error: the parent material could not be read: " << parentPath << '\n';
    }
    return parent;
}

// One "Parameter=value" applied to the instance: a parameter the parent's graph exposes, else a
// built-in PBR field (Base Color, Roughness, ..., Base Color Texture); the reason it was refused.
[[nodiscard]] std::string ApplyInstanceSet(
    kb::render::RenderMaterialInstanceAssetData& instance,
    const kb::render::RenderMaterialAssetData& parent,
    const std::string& spec,
    const material::TextureResolver& textures) {
    const auto [name, text] = material::SplitSet(spec);
    if (name.empty()) {
        return "--set takes Parameter=value (e.g. --set Color=#FF0000), got '" + spec + "'";
    }
    const std::vector<ExposedParameter> parameters = ExposedParameters(parent);
    if (const ExposedParameter* parameter = FindParameter(parameters, name)) {
        std::string error;
        std::optional<kb::render::RenderMaterialGraphParameterValue> value = ParameterValue(*parameter, text, textures, error);
        if (!value.has_value()) return error;
        const kb::render::RenderMaterialInstanceValidationResult validation =
            kb::render::SetRenderMaterialInstanceParameterOverride(instance, parent, std::move(*value));
        if (!validation.Succeeded()) {
            return validation.diagnostics.front().message;
        }
        return {};
    }
    kb::render::RenderMaterialInstanceAssetData edited = instance;
    kb::render::EnsureRenderMaterialInstanceOverrideDocument(edited, parent);
    bool known = false;
    std::string problem = material::ApplyBuiltinField(edited.overrides.desc, name, text, textures, known);
    if (!known) {
        return "the parent material has no parameter or field '" + name + "'; parameters: " + ParameterList(parameters) +
            "; fields: " + material::BuiltinFieldList();
    }
    if (!problem.empty()) return problem;
    const kb::render::RenderMaterialInstanceValidationResult validation =
        kb::render::RenderMaterialInstanceAssetLoader::ValidateAgainstParent(edited, parent);
    if (!validation.Succeeded()) return validation.diagnostics.front().message;
    instance = std::move(edited);
    return {};
}

void PrintInstance(
    const kb::render::RenderMaterialInstanceAssetData& instance,
    const kb::render::RenderMaterialAssetData& parent,
    const std::string& virtualPath,
    const std::string& parentPath,
    const kb::assets::AssetRegistry& registry,
    CommandIo io,
    kb::core::JsonValue* data = nullptr) {
    using kb::core::JsonValue;
    io.out << virtualPath << '\n' << "  Parent = " << parentPath << '\n';
    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, instance);
    const std::vector<ExposedParameter> parameters = ExposedParameters(parent);
    auto parameterRows = JsonValue::MakeArray();
    for (const ExposedParameter& parameter : parameters) {
        const auto value = std::ranges::find_if(effective.graphParameterValues, [&](const kb::render::RenderMaterialGraphParameterValue& candidate) {
            return candidate.stableId == parameter.stableId;
        });
        const bool overridden = instance.hasOverrides && std::ranges::any_of(instance.overrides.graphParameterValues,
            [&](const kb::render::RenderMaterialGraphParameterValue& candidate) { return candidate.stableId == parameter.stableId; });
        io.out << "  " << parameter.displayName << " = "
               << (value != effective.graphParameterValues.end() ? ParameterValueText(*value, registry) : std::string{ "(graph default)" })
               << (overridden ? " (override)" : "") << '\n';
        if (data != nullptr) {
            auto row = JsonValue::MakeObject();
            row.Set("name", JsonValue::MakeString(parameter.displayName));
            row.Set("value", JsonValue::MakeString(value != effective.graphParameterValues.end() ? ParameterValueText(*value, registry) : std::string{ "(graph default)" }));
            row.Set("override", JsonValue::MakeBool(overridden));
            parameterRows.Append(std::move(row));
        }
    }
    if (data != nullptr) {
        data->Set("instance", JsonValue::MakeString(virtualPath));
        data->Set("parent", JsonValue::MakeString(parentPath));
        data->Set("parameters", std::move(parameterRows));
        data->Set("values", JsonValue::MakeNull());
    }
    // A graph material renders its graph; the built-in values are only what a material without
    // one shows.
    if (!kb::render::HasGraphAuthoringData(effective.graph)) {
        material::PrintBuiltinValues(effective.desc, &registry, io);
        if (data != nullptr) data->Set("values", material::BuiltinValuesJson(effective.desc, &registry));
    }
}

[[nodiscard]] std::filesystem::path UniqueFile(const std::filesystem::path& folder, const std::string& baseName, std::string_view extension) {
    std::filesystem::path candidate = folder / kb::assets::Utf8Path(baseName + std::string{ extension });
    for (std::uint32_t suffix = 1U; std::filesystem::exists(candidate); ++suffix) {
        candidate = folder / kb::assets::Utf8Path(baseName + std::to_string(suffix) + std::string{ extension });
    }
    return candidate;
}

} // namespace

namespace material {

std::string ApplyGraphText(kb::render::RenderMaterialAssetData& asset, std::string_view text, const TextureResolver& textures, const kb::assets::AssetRegistry& registry) {
    return graph_text::Apply(asset, text, textures, registry);
}

std::string GraphText(const kb::render::RenderMaterialAssetData& asset, const kb::assets::AssetRegistry& registry) {
    return graph_text::Text(asset, registry);
}

std::vector<std::string> MaterialProblems(const kb::render::RenderMaterialAssetData& asset, const kb::assets::AssetRegistry& registry) {
    return CheckMaterial(asset, registry).problems;
}

} // namespace material

int RunMaterialGraphCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    return GraphCommand(positionals.empty() ? std::string_view{} : std::string_view{ positionals.front() }, arguments, io);
}

int RunMaterialInstanceNewCommand(const ArgumentList& arguments, CommandIo io) {
    LoadedProject project{ .root = material::ProjectRoot(arguments, "material-instance-new", io) };
    const std::optional<std::string> parentText = arguments.Option("--parent");
    if (project.root.empty() || !parentText.has_value()) {
        if (!project.root.empty()) {
            io.err << "error: material-instance-new requires --project <dir> --parent /Game/<path>.kbmat [--name <Name>]\n";
        }
        return 1;
    }
    const std::optional<material::ProjectAsset> parentFile = material::ProjectAssetFile(
        project.root, *parentText, kMaterialExtension, "material", "an instance's parent is a .kbmat; create one with kb_cli material-new", io);
    if (!parentFile.has_value() || !Mount(project, io)) return 1;
    const kb::assets::AssetMetadata* parentMetadata = Registry(project).FindByPath(kb::assets::Utf8Path(parentFile->virtualPath));
    if (parentMetadata == nullptr || parentMetadata->type != "RenderMaterial") {
        io.err << "error: " << parentFile->virtualPath << " is not a material asset of the project\n";
        return 1;
    }
    std::string parentPath;
    const std::optional<kb::render::RenderMaterialAssetData> parent = ReadParent(project, parentMetadata->id, parentPath, io);
    if (!parent.has_value()) return 1;

    // Beside its parent unless --folder says otherwise, as the editor's Create Material Instance.
    std::filesystem::path folder = parentFile->file.parent_path();
    std::string folderVirtualPath = parentFile->virtualPath.substr(0U, parentFile->virtualPath.rfind('/'));
    if (const std::optional<std::string> folderText = arguments.Option("--folder")) {
        const kb::assets::ProjectAssetFolder resolved = kb::assets::ResolveProjectAssetFolder(project.root, *folderText);
        if (!resolved.error.empty()) {
            io.err << "error: " << resolved.error << '\n';
            return 1;
        }
        folder = resolved.folder;
        folderVirtualPath = resolved.virtualPath;
    }
    std::filesystem::path file;
    if (const std::optional<std::string> name = arguments.Option("--name")) {
        if (const std::string problem = kb::assets::AssetFileNameError(*name); !problem.empty()) {
            io.err << "error: --name '" << *name << "': " << problem << '\n';
            return 1;
        }
        file = folder / kb::assets::Utf8Path(*name + kInstanceExtension);
        if (std::filesystem::exists(file)) {
            io.err << "error: material instance already exists: " << folderVirtualPath << '/' << *name << kInstanceExtension
                   << " (change it with kb_cli material-instance-set)\n";
            return 1;
        }
    } else {
        file = UniqueFile(folder, parentFile->file.stem().string() + "Instance", kInstanceExtension);
    }
    const std::string virtualPath = folderVirtualPath + "/" + material::Utf8Text(file.filename());

    kb::render::RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMetadata->id;
    const material::TextureResolver textures = material::ProjectTextureResolver(Registry(project));
    for (const std::string& spec : arguments.Options("--set")) {
        if (const std::string problem = ApplyInstanceSet(instance, *parent, spec, textures); !problem.empty()) {
            io.err << "error: " << problem << '\n';
            return 1;
        }
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (!kb::render::RenderMaterialInstanceAssetWriter::Save(file, instance)) {
        io.err << "error: the material instance could not be written: " << material::Utf8Text(file) << '\n';
        return 1;
    }
    PrintInstance(instance, *parent, virtualPath, parentPath, Registry(project), io);
    io.out << "Use it in a Mesh Renderer: --set material=" << virtualPath << '\n';
    return 0;
}

namespace {

int InstanceEdit(const ArgumentList& arguments, CommandIo io, bool change) {
    const char* command = change ? "material-instance-set" : "material-instance-show";
    LoadedProject project{ .root = material::ProjectRoot(arguments, command, io) };
    const std::optional<std::string> instanceText = arguments.Option("--instance");
    const std::vector<std::string> sets = arguments.Options("--set");
    const std::vector<std::string> clears = arguments.Options("--clear");
    if (project.root.empty() || !instanceText.has_value() || (change && sets.empty() && clears.empty())) {
        if (!project.root.empty()) {
            io.err << "error: " << command << " requires --project <dir> --instance /Game/<path>.kbmatinst"
                   << (change ? " --set Parameter=value [...] | --clear Parameter" : "") << '\n';
        }
        return 1;
    }
    const std::optional<material::ProjectAsset> located = material::ProjectAssetFile(
        project.root, *instanceText, kInstanceExtension, "material instance", "create one with kb_cli material-instance-new", io);
    if (!located.has_value() || !Mount(project, io)) return 1;
    std::optional<kb::render::RenderMaterialInstanceAssetData> instance =
        kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(located->file);
    if (!instance.has_value()) {
        io.err << "error: the material instance could not be read: " << located->virtualPath << '\n';
        return 1;
    }
    std::string parentPath;
    const std::optional<kb::render::RenderMaterialAssetData> parent = ReadParent(project, instance->parentMaterialAssetId, parentPath, io);
    if (!parent.has_value()) return 1;
    if (change) {
        const material::TextureResolver textures = material::ProjectTextureResolver(Registry(project));
        for (const std::string& name : clears) {
            const std::vector<ExposedParameter> parameters = ExposedParameters(*parent);
            const ExposedParameter* parameter = FindParameter(parameters, name);
            if (parameter == nullptr || !instance->hasOverrides) {
                io.err << "error: the instance has no override of '" << name << "'\n";
                return 1;
            }
            kb::render::RemoveRenderMaterialGraphParameterValue(instance->overrides, parameter->stableId);
        }
        for (const std::string& spec : sets) {
            if (const std::string problem = ApplyInstanceSet(*instance, *parent, spec, textures); !problem.empty()) {
                io.err << "error: " << problem << '\n';
                return 1;
            }
        }
        if (!kb::render::RenderMaterialInstanceAssetWriter::Save(located->file, *instance)) {
            io.err << "error: the material instance could not be written: " << located->virtualPath << '\n';
            return 1;
        }
    }
    kb::core::JsonValue data = DataObject("21kb.cli.material-instance-show/v1");
    PrintInstance(*instance, *parent, located->virtualPath, parentPath, Registry(project), io, !change && io.WantsData() ? &data : nullptr);
    if (!change) PublishData(io, std::move(data));
    return 0;
}

} // namespace

int RunMaterialInstanceSetCommand(const ArgumentList& arguments, CommandIo io) {
    return InstanceEdit(arguments, io, true);
}

int RunMaterialInstanceShowCommand(const ArgumentList& arguments, CommandIo io) {
    return InstanceEdit(arguments, io, false);
}

} // namespace kb::cli
