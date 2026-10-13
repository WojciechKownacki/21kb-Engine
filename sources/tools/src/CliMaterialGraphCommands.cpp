#include "CliCommands.hpp"

#include <cmath>
#include <cstdlib>
#include "CliCommon.hpp"
#include "CliMaterialShared.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderMaterialAuthoring.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"

#include <algorithm>
#include <charconv>
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

// Unreal's names for the common nodes, beside the engine's own kind names.
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

// The parameters a material exposes (Unreal's parameter list): name, id and type.
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
// graph and shader-generation errors are reported with it (Unreal saves such a material too,
// and the editor shows the same errors).
struct MaterialCheck {
    bool loads = true;
    std::vector<std::string> problems;
};

[[nodiscard]] MaterialCheck CheckMaterial(const kb::render::RenderMaterialAssetData& asset) {
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
            check.problems.push_back(std::string{ kb::render::RenderMaterialGraphDiagnosticKindName(diagnostic.kind) } + ": " + diagnostic.message);
        }
    }
    if (kb::render::HasGraphAuthoringData(asset.graph)) {
        const kb::render::RenderMaterialGraphCompileResult compiled = kb::render::CompileRenderMaterialGraphToShaderSource(asset.graph);
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
            if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) {
                check.problems.push_back("shader: " + diagnostic.message);
            }
        }
    }
    return check;
}

void PrintGraph(const kb::render::RenderMaterialAssetData& asset, const std::string& virtualPath,
    const kb::assets::AssetRegistry& registry, CommandIo io) {
    io.out << virtualPath << '\n' << "Nodes:\n";
    for (const kb::render::RenderMaterialGraphNode& node : asset.graph.nodes) {
        io.out << "  " << NodeLabel(node);
        if (kb::render::IsRenderMaterialGraphConstantNode(node.kind)) {
            io.out << " = " << node.parameter.defaultValueHint;
        }
        io.out << "\n    in: " << JoinNames(kb::render::RenderMaterialGraphNodeInputPinNames(node))
               << "\n    out: " << JoinNames(kb::render::RenderMaterialGraphNodeOutputPinNames(node)) << '\n';
    }
    io.out << "Links:\n";
    for (const kb::render::RenderMaterialGraphLink& link : asset.graph.links) {
        io.out << "  " << link.fromNodeId << '.' << link.fromPin << " -> " << link.toNodeId << '.' << link.toPin << '\n';
    }
    const std::vector<ExposedParameter> parameters = ExposedParameters(asset);
    io.out << "Parameters:\n";
    for (const ExposedParameter& parameter : parameters) {
        io.out << "  " << parameter.displayName << " (" << parameter.stableId << ")";
        const auto value = std::ranges::find_if(asset.graphParameterValues, [&](const kb::render::RenderMaterialGraphParameterValue& candidate) {
            return candidate.stableId == parameter.stableId;
        });
        if (value != asset.graphParameterValues.end()) io.out << " = " << ParameterValueText(*value, registry);
        io.out << '\n';
    }
    const MaterialCheck check = CheckMaterial(asset);
    io.out << (check.problems.empty() ? "Compiles: yes\n" : "Compiles: no\n");
    for (const std::string& problem : check.problems) io.out << "  " << problem << '\n';
}

[[nodiscard]] bool SaveMaterial(const kb::render::RenderMaterialAssetData& asset, const material::ProjectAsset& located, CommandIo io) {
    const MaterialCheck check = CheckMaterial(asset);
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
        // Unreal's Noise settings: "<function> <turbulence> <scale> <quality> <levels> <level scale> <min> <max>".
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

// The material-wide settings of the Material Editor's Details: Domain, Shading Model, Blend Mode.
[[nodiscard]] std::string SetGraphSetting(kb::render::RenderMaterialAssetData& asset, std::string_view name, std::string_view value) {
    const std::string wanted = material::Normalized(name);
    const std::string text = material::Normalized(value);
    if (wanted == "domain") {
        const std::string_view canonical = kb::render::RenderMaterialDomainName(kb::render::ParseRenderMaterialDomain(value));
        if (material::Normalized(canonical) != text) return "Domain does not take '" + std::string{ value } + "'";
        asset.graph.materialDomain = std::string{ canonical };
        return {};
    }
    if (wanted == "shadingmodel") {
        const std::string_view canonical = kb::render::RenderMaterialShadingModelName(kb::render::ParseRenderMaterialShadingModel(value));
        if (material::Normalized(canonical) != text) return "Shading Model does not take '" + std::string{ value } + "'";
        asset.graph.shadingModel = std::string{ canonical };
        return {};
    }
    if (wanted == "blendmode") {
        const std::string_view canonical = kb::render::RenderMaterialGraphBlendModeName(kb::render::ParseRenderMaterialGraphBlendMode(value));
        if (material::Normalized(canonical) != text) return "Blend Mode does not take '" + std::string{ value } + "'";
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

int GraphCommand(std::string_view action, const ArgumentList& arguments, CommandIo io) {
    if (action == "kinds") {
        for (const kb::render::RenderMaterialGraphNodeKind kind : kb::render::AllRenderMaterialGraphNodeKinds()) {
            io.out << kb::render::RenderMaterialGraphNodeKindName(kind)
                   << "  in: " << JoinNames(kb::render::RenderMaterialGraphNodeInputPinNames(kind))
                   << "  out: " << JoinNames(kb::render::RenderMaterialGraphNodeOutputPinNames(kind)) << '\n';
        }
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
    if (action == "add-node") {
        const std::optional<std::string> kindText = arguments.Option("--kind");
        const std::optional<kb::render::RenderMaterialGraphNodeKind> kind = kindText.has_value() ? ParseKind(*kindText) : std::nullopt;
        if (!kind.has_value()) {
            io.err << "error: material-graph add-node requires --kind <node kind> (e.g. ScalarParameter, VectorParameter, "
                      "TextureParameter, Constant, Multiply; kb_cli material-graph kinds lists them)\n";
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
        if (!SaveMaterial(*asset, *located, io)) return 1;
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
    CommandIo io) {
    io.out << virtualPath << '\n' << "  Parent = " << parentPath << '\n';
    const kb::render::RenderMaterialAssetData effective = kb::render::BuildEffectiveRenderMaterialInstanceAsset(parent, instance);
    const std::vector<ExposedParameter> parameters = ExposedParameters(parent);
    for (const ExposedParameter& parameter : parameters) {
        const auto value = std::ranges::find_if(effective.graphParameterValues, [&](const kb::render::RenderMaterialGraphParameterValue& candidate) {
            return candidate.stableId == parameter.stableId;
        });
        const bool overridden = instance.hasOverrides && std::ranges::any_of(instance.overrides.graphParameterValues,
            [&](const kb::render::RenderMaterialGraphParameterValue& candidate) { return candidate.stableId == parameter.stableId; });
        io.out << "  " << parameter.displayName << " = "
               << (value != effective.graphParameterValues.end() ? ParameterValueText(*value, registry) : std::string{ "(graph default)" })
               << (overridden ? " (override)" : "") << '\n';
    }
    // A graph material renders its graph; the built-in values are only what a material without
    // one shows.
    if (!kb::render::HasGraphAuthoringData(effective.graph)) {
        material::PrintBuiltinValues(effective.desc, &registry, io);
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
    PrintInstance(*instance, *parent, located->virtualPath, parentPath, Registry(project), io);
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
