#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialAuthoring.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// material-function: a reusable piece of material graph (the engine's Material Function, .kbmatfn) made without the
// Material Editor: `new` writes one from a preset (a glowing edge, a scrolling texture coordinate, a tiling one),
// `use` puts a call to it in a material's graph, gives every input a value and wires an output to a material output
// (Emissive for a colour, Customized UV for a texture coordinate). Saved and checked as the editor saves a material.

namespace kb::cli {
namespace {

using kb::render::RenderMaterialGraphDocument;
using kb::render::RenderMaterialGraphNodeKind;
using kb::render::RenderMaterialGraphPinType;
using Kind = RenderMaterialGraphNodeKind;

constexpr std::string_view kUsage =
    "usage: kb_cli material-function <new|show|use> --project <dir> ...\n"
    "  new  --name <Name> --preset fresnel-glow|panner|tiling [--folder Assets/Materials] [--replace]\n"
    "  show --function <path|Name>\n"
    "  use  (--entity <Object> | --material <path>) --function <path|Name> [--to emissive|<material output>] [--output <function output>] [--set Input=value ...]\n"
    "       (a colour as #RRGGBB, a number as 3; an input without --set takes the default `show` lists)\n";

struct PresetInfo {
    std::string_view name;
    std::string_view meaning;
};

constexpr PresetInfo kPresets[]{
    { "fresnel-glow", "a glow on the edge of an object: inputs Color, Power (how thin the edge is), Intensity; output Glow (a colour, for Emissive)" },
    { "panner", "a texture coordinate that scrolls sideways over time: input Speed; output UV (for Customized UV)" },
    { "tiling", "a texture coordinate repeated Tiling times: input Tiling; output UV (for Customized UV)" },
};

[[nodiscard]] std::string PresetList() {
    std::string list;
    for (const PresetInfo& preset : kPresets) list += (list.empty() ? "" : ", ") + std::string{ preset.name };
    return list;
}

// Builds a function graph node by node with the editor's own document edits.
class Builder {
public:
    Builder() { graph_.storageModel = "material-function-asset"; }

    // The finished function: a function holds its inputs, its body and its outputs, no Material Output.
    [[nodiscard]] RenderMaterialGraphDocument Finished() const {
        RenderMaterialGraphDocument finished = graph_;
        std::erase_if(finished.nodes, [](const kb::render::RenderMaterialGraphNode& node) { return node.kind == Kind::MaterialOutput; });
        return finished;
    }

    std::uint32_t Add(Kind kind, int x, int y) {
        const std::optional<std::uint32_t> id = kb::render::AddRenderMaterialGraphNode(graph_, kind, x, y);
        if (!id.has_value()) ok_ = false;
        return id.value_or(0U);
    }

    std::uint32_t Input(const std::string& name, RenderMaterialGraphPinType type, const std::string& defaultValue, int y) {
        return Endpoint(Kind::FunctionInput, name, type, "default " + defaultValue, 60, y);
    }

    std::uint32_t Output(const std::string& name, RenderMaterialGraphPinType type, int x, int y) { return Endpoint(Kind::FunctionOutput, name, type, {}, x, y); }

    void Connect(std::uint32_t from, std::string_view fromPin, std::uint32_t to, std::string_view toPin) {
        if (!kb::render::ConnectRenderMaterialGraphPins(graph_, from, fromPin, to, toPin)) ok_ = false;
    }

    void Value(std::uint32_t node, std::string_view value) {
        if (!kb::render::SetRenderMaterialGraphConstantValue(graph_, node, value)) ok_ = false;
    }

    void Setting(std::uint32_t node, std::string_view value) {
        for (kb::render::RenderMaterialGraphNode& candidate : graph_.nodes) {
            if (candidate.id == node) candidate.parameter.defaultValueHint = std::string{ value };
        }
    }

    [[nodiscard]] bool Ok() const { return ok_; }

private:
    std::uint32_t Endpoint(Kind kind, const std::string& name, RenderMaterialGraphPinType type, const std::string& note, int x, int y) {
        const std::uint32_t id = Add(kind, x, y);
        for (kb::render::RenderMaterialGraphNode& node : graph_.nodes) {
            if (node.id != id) continue;
            node.parameter.stableId = name;
            node.parameter.displayName = name;
            node.parameter.defaultValueHint = std::string{ kb::render::RenderMaterialGraphPinTypeName(type) };
            node.parameter.description = note;
            node.parameter.overrideSupported = false;
        }
        return id;
    }

    RenderMaterialGraphDocument graph_;
    bool ok_ = true;
};

[[nodiscard]] std::optional<RenderMaterialGraphDocument> BuildPreset(std::string_view preset) {
    Builder b;
    if (preset == "fresnel-glow") {
        const std::uint32_t color = b.Input("Color", RenderMaterialGraphPinType::Color, "#FF8C1A", 40);
        const std::uint32_t power = b.Input("Power", RenderMaterialGraphPinType::Float, "3", 160);
        const std::uint32_t intensity = b.Input("Intensity", RenderMaterialGraphPinType::Float, "2", 280);
        const std::uint32_t fresnel = b.Add(Kind::Fresnel, 260, 160);
        const std::uint32_t tint = b.Add(Kind::Multiply, 460, 80);
        const std::uint32_t boost = b.Add(Kind::Multiply, 660, 120);
        const std::uint32_t glow = b.Output("Glow", RenderMaterialGraphPinType::Color, 860, 120);
        b.Connect(power, "value", fresnel, "exponent");
        b.Connect(fresnel, "value", tint, "a");
        b.Connect(color, "value", tint, "b");
        b.Connect(tint, "value", boost, "a");
        b.Connect(intensity, "value", boost, "b");
        b.Connect(boost, "value", glow, "value");
        return b.Ok() ? std::optional<RenderMaterialGraphDocument>{ b.Finished() } : std::nullopt;
    }
    if (preset == "panner") {
        const std::uint32_t speed = b.Input("Speed", RenderMaterialGraphPinType::Float, "0.2", 200);
        const std::uint32_t uv = b.Add(Kind::TextureCoordinate, 60, 40);
        const std::uint32_t time = b.Add(Kind::Time, 60, 320);
        const std::uint32_t scaled = b.Add(Kind::Multiply, 260, 260);
        const std::uint32_t pan = b.Add(Kind::Panner, 460, 120);
        const std::uint32_t out = b.Output("UV", RenderMaterialGraphPinType::Float2, 680, 120);
        b.Setting(pan, "1 0");
        b.Connect(time, "value", scaled, "a");
        b.Connect(speed, "value", scaled, "b");
        b.Connect(uv, "uv", pan, "coordinate");
        b.Connect(scaled, "value", pan, "time");
        b.Connect(pan, "uv", out, "value");
        return b.Ok() ? std::optional<RenderMaterialGraphDocument>{ b.Finished() } : std::nullopt;
    }
    if (preset == "tiling") {
        const std::uint32_t tiling = b.Input("Tiling", RenderMaterialGraphPinType::Float, "2", 200);
        const std::uint32_t uv = b.Add(Kind::TextureCoordinate, 60, 40);
        const std::uint32_t scaled = b.Add(Kind::Multiply, 300, 120);
        const std::uint32_t out = b.Output("UV", RenderMaterialGraphPinType::Float2, 540, 120);
        b.Connect(uv, "uv", scaled, "a");
        b.Connect(tiling, "value", scaled, "b");
        b.Connect(scaled, "value", out, "value");
        return b.Ok() ? std::optional<RenderMaterialGraphDocument>{ b.Finished() } : std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
    return joined.empty() ? "none" : joined;
}

struct OpenFunction {
    std::filesystem::path file;
    std::string virtualPath;
    std::uint64_t assetId = 0U;
    kb::render::RenderMaterialFunctionAssetData data;
};

[[nodiscard]] bool Mount(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    std::string error;
    if (!project.has_value()) {
        io.err << "error: material-function requires --project <dir>\n" << kUsage;
        return false;
    }
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(*project), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<OpenFunction> LoadFunction(const ArgumentList& arguments, kb::scene::Scene& scene, CommandIo io) {
    const std::optional<std::string> text = arguments.Option("--function");
    const kb::assets::AssetMetadata* metadata = nullptr;
    if (text.has_value()) {
        const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
        metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(*text));
        if (metadata == nullptr && text->find('/') == std::string::npos && text->find('\\') == std::string::npos) {
            metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path("/Game/Materials/" + *text + kb::render::kRenderMaterialFunctionAssetExtension));
        }
    }
    if (metadata == nullptr || metadata->type != kb::render::kRenderMaterialFunctionAssetType) {
        io.err << "error: --function must name a material function of the project, like /Game/Materials/GlowEdge.kbmatfn (make one with kb_cli material-function new)\n";
        return std::nullopt;
    }
    std::optional<kb::render::RenderMaterialFunctionAssetData> loaded = kb::render::RenderMaterialFunctionAssetLoader::LoadFunction(metadata->physicalPath);
    if (!loaded.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read\n";
        return std::nullopt;
    }
    return OpenFunction{ metadata->physicalPath, metadata->virtualPath.generic_string(), metadata->id.value, std::move(*loaded) };
}

// What an input is given when `use` is not told: the default the function was made with, else a value of its type.
[[nodiscard]] std::string InputDefault(const kb::render::RenderMaterialGraphNode& node, RenderMaterialGraphPinType type) {
    if (node.parameter.description.starts_with("default ")) return node.parameter.description.substr(8U);
    switch (type) {
    case RenderMaterialGraphPinType::Color:
    case RenderMaterialGraphPinType::Float4: return "#FFFFFF";
    case RenderMaterialGraphPinType::Float3: return "1 1 1";
    case RenderMaterialGraphPinType::Float2: return "0 0";
    default: return "1";
    }
}

struct Endpoint {
    std::string name;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Float4;
    std::string defaultValue;
};

[[nodiscard]] std::vector<Endpoint> Inputs(const RenderMaterialGraphDocument& graph) {
    std::vector<Endpoint> inputs;
    const kb::render::RenderMaterialGraphCustomCode signature = kb::render::BuildRenderMaterialFunctionCallCustomCode(graph);
    std::vector<const kb::render::RenderMaterialGraphNode*> nodes;
    for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == Kind::FunctionInput) nodes.push_back(&node);
    }
    std::ranges::sort(nodes, {}, &kb::render::RenderMaterialGraphNode::id);
    for (std::size_t index = 0U; index < signature.inputs.size(); ++index) {
        inputs.push_back({ signature.inputs[index].name, signature.inputs[index].type,
            index < nodes.size() ? InputDefault(*nodes[index], signature.inputs[index].type) : std::string{} });
    }
    return inputs;
}

[[nodiscard]] std::vector<Endpoint> Outputs(const RenderMaterialGraphDocument& graph) {
    std::vector<Endpoint> outputs;
    for (const kb::render::RenderMaterialGraphCustomPin& pin : kb::render::BuildRenderMaterialFunctionCallCustomCode(graph).outputs) outputs.push_back({ pin.name, pin.type, {} });
    return outputs;
}

[[nodiscard]] std::string TypeName(RenderMaterialGraphPinType type) { return std::string{ kb::render::RenderMaterialGraphPinTypeName(type) }; }

// The problems the editor's Save reports for a material that calls functions: a document that would not load again, graph
// errors, and errors of the shader the graph makes (with the function's graph inlined).
struct Check {
    bool loads = true;
    std::vector<std::string> problems;
};

[[nodiscard]] Check CheckMaterial(const kb::render::RenderMaterialAssetData& asset, const kb::assets::AssetRegistry& registry) {
    Check check;
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
    const kb::render::RenderMaterialGraphFunctionLibrary library = material::FunctionLibraryOf(asset.graph, registry);
    kb::render::RenderMaterialGraphBuildContext context;
    context.functionLibrary = &library;
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : kb::render::CompileRenderMaterialGraphToShaderSource(asset.graph, context).diagnostics) {
        if (diagnostic.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error) check.problems.push_back("shader: " + diagnostic.message);
    }
    return check;
}

[[nodiscard]] std::string ShowFunction(const OpenFunction& function) {
    std::ostringstream text;
    text << "function " << function.virtualPath << "\n  inputs:\n";
    for (const Endpoint& input : Inputs(function.data.graph)) text << "    " << input.name << " (" << TypeName(input.type) << ") default " << input.defaultValue << '\n';
    text << "  outputs:\n";
    for (const Endpoint& output : Outputs(function.data.graph)) text << "    " << output.name << " (" << TypeName(output.type) << ")\n";
    text << "  body: ";
    std::string body;
    for (const kb::render::RenderMaterialGraphNode& node : function.data.graph.nodes) {
        if (node.kind == Kind::FunctionInput || node.kind == Kind::FunctionOutput) continue;
        body += (body.empty() ? "" : ", ") + std::string{ kb::render::RenderMaterialGraphNodeKindName(node.kind) };
    }
    text << (body.empty() ? "none" : body) << "\n  use it: kb_cli material-function use --material /Game/Materials/<Name>.kbmat --function " << function.virtualPath << '\n';
    return text.str();
}

[[nodiscard]] int New(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::string preset = arguments.Option("--preset").value_or(std::string{});
    if (preset.empty()) {
        io.err << "error: material-function new requires --preset " << PresetList() << '\n';
        for (const PresetInfo& info : kPresets) io.err << "  " << info.name << ": " << info.meaning << '\n';
        return 1;
    }
    const std::optional<RenderMaterialGraphDocument> graph = BuildPreset(preset);
    if (!graph.has_value()) {
        io.err << "error: unknown preset '" << preset << "'; presets: " << PresetList() << '\n';
        return 1;
    }
    const std::optional<AssetTarget> target = ResolveAssetTarget(arguments, "material-function new", "--project <dir> --name <Name> --preset <preset>", "Assets/Materials",
        kb::render::kRenderMaterialFunctionAssetExtension, false, io);
    if (!target.has_value()) return 1;
    kb::render::RenderMaterialFunctionAssetData data;
    data.graph = *graph;
    std::error_code error;
    std::filesystem::create_directories(target->file.parent_path(), error);
    if (!kb::render::RenderMaterialFunctionAssetLoader::SaveFunction(target->file, data) ||
        !kb::render::RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(target->file).asset.has_value()) {
        io.err << "error: the function could not be written or read back: " << target->virtualPath << '\n';
        return 1;
    }
    OpenFunction made{ target->file, target->virtualPath, 0U, std::move(data) };
    io.out << "created " << target->virtualPath << " (" << preset << ")\n" << ShowFunction(made);
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::optional<OpenFunction> function = LoadFunction(arguments, scene, io);
    if (!function.has_value()) return 1;
    io.out << ShowFunction(*function);
    return 0;
}

// The material of a scene object (--entity): the one its Mesh Renderer uses, as a project path. Empty with `error` set otherwise.
[[nodiscard]] std::string MaterialOfObject(const ArgumentList& arguments, const kb::assets::AssetRegistry& registry, const std::string& object, std::string& error) {
    std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, error);
    if (!loaded.has_value()) return {};
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded->document.worldPrefab.Nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].name != object && NodePath(nodes, index) != object) continue;
        const auto& renderer = nodes[index].components.meshRenderer;
        const kb::assets::AssetMetadata* metadata = renderer.has_value() && renderer->materialAssetId != 0U ? registry.Find(kb::assets::AssetId{ renderer->materialAssetId }) : nullptr;
        if (metadata == nullptr || metadata->type != "RenderMaterial") {
            error = "'" + object + "' has no material of its own (kb_cli material-new --name <Name>, then component set --entity " + object + " --type MeshRenderer --set material=/Game/Materials/<Name>.kbmat)";
            return {};
        }
        return metadata->virtualPath.generic_string();
    }
    error = "the scene has no object named '" + object + "' (kb_cli scene-list shows the names)";
    return {};
}

[[nodiscard]] std::string PinOf(const std::vector<std::string>& pins, std::string_view wanted) {
    const auto found = std::ranges::find_if(pins, [&](const std::string& pin) { return material::Normalized(pin) == material::Normalized(wanted); });
    return found != pins.end() ? *found : std::string{};
}

[[nodiscard]] int Use(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    if (!Mount(arguments, scene, io)) return 1;
    const std::optional<OpenFunction> function = LoadFunction(arguments, scene, io);
    if (!function.has_value()) return 1;
    const std::filesystem::path root = material::ProjectRoot(arguments, "material-function", io);
    std::optional<std::string> materialText = arguments.Option("--material");
    if (!materialText.has_value() && arguments.Option("--entity").has_value()) {
        std::string objectError;
        const std::string path = MaterialOfObject(arguments, scene.Assets().Manager().Registry(), *arguments.Option("--entity"), objectError);
        if (path.empty()) {
            io.err << "error: " << objectError << '\n';
            return 1;
        }
        materialText = path;
    }
    if (root.empty() || !materialText.has_value()) {
        if (!root.empty()) io.err << "error: material-function use requires --entity <Object> (the object whose material gets the effect) or --material /Game/<path>.kbmat\n";
        return 1;
    }
    const std::optional<material::ProjectAsset> located = material::ProjectAssetFile(root, *materialText, ".kbmat", "material", "create one with kb_cli material-new", io);
    if (!located.has_value()) return 1;
    std::optional<kb::render::RenderMaterialAssetData> asset = kb::render::RenderMaterialAssetLoader::LoadMaterial(located->file);
    if (!asset.has_value()) {
        io.err << "error: the material could not be read: " << located->virtualPath << '\n';
        return 1;
    }
    // A material whose graph does not colour the surface yet colours it by its own colour value: that colour
    // becomes a constant wired to Base Color, so the object keeps its colour next to the new effect.
    kb::render::EnsureEditableRenderMaterialGraph(asset->graph);
    const bool plain = std::ranges::none_of(asset->graph.links, [](const kb::render::RenderMaterialGraphLink& link) { return link.toPin == "baseColor"; });
    RenderMaterialGraphDocument& graph = asset->graph;

    const std::vector<Endpoint> inputs = Inputs(function->data.graph);
    const std::vector<Endpoint> outputs = Outputs(function->data.graph);
    if (outputs.empty()) {
        io.err << "error: the function has no output to use\n";
        return 1;
    }
    std::vector<std::pair<std::string, std::string>> given;
    for (const std::string& setting : arguments.Options("--set")) {
        const auto [name, value] = material::SplitSet(setting);
        const auto input = std::ranges::find_if(inputs, [&](const Endpoint& candidate) { return material::Normalized(candidate.name) == material::Normalized(name); });
        if (name.empty() || input == inputs.end()) {
            io.err << "error: --set " << setting << ": the function's inputs are ";
            for (const Endpoint& candidate : inputs) io.err << candidate.name << ' ';
            io.err << "(--set Input=value)\n";
            return 1;
        }
        given.emplace_back(input->name, value);
    }
    const std::string outputWanted = arguments.Option("--output").value_or(outputs.front().name);
    const auto output = std::ranges::find_if(outputs, [&](const Endpoint& candidate) { return material::Normalized(candidate.name) == material::Normalized(outputWanted); });
    if (output == outputs.end()) {
        io.err << "error: the function has no output '" << outputWanted << "'\n";
        return 1;
    }

    const std::optional<std::uint32_t> callId = kb::render::AddRenderMaterialGraphNode(graph, Kind::MaterialFunctionCall, 360, static_cast<std::int32_t>(graph.nodes.size()) * 120);
    kb::render::RenderMaterialGraphNode* call = callId.has_value() ? kb::render::FindMutableRenderMaterialGraphNode(graph, *callId) : nullptr;
    if (call == nullptr) {
        io.err << "error: a function call cannot be added to this material\n";
        return 1;
    }
    call->parameter.stableId = std::to_string(function->assetId);
    call->parameter.defaultValueHint.clear();
    call->customCode = kb::render::BuildRenderMaterialFunctionCallCustomCode(function->data.graph);
    static_cast<void>(kb::render::RenameRenderMaterialGraphNode(graph, *callId, std::filesystem::path{ function->virtualPath }.stem().string(), "Material Function"));

    std::size_t row = 0U;
    for (const Endpoint& input : inputs) {
        Kind kind = Kind::ConstantScalar;
        std::string pin = "value";
        std::size_t channels = 0U;
        switch (input.type) {
        case RenderMaterialGraphPinType::Float2: kind = Kind::ConstantVector2; pin = "xy"; break;
        case RenderMaterialGraphPinType::Float3: kind = Kind::ConstantVector; pin = "xyz"; channels = 3U; break;
        case RenderMaterialGraphPinType::Float4:
        case RenderMaterialGraphPinType::Color: kind = Kind::ConstantColor; pin = "rgba"; channels = 4U; break;
        default: break;
        }
        std::string value = input.defaultValue;
        for (const auto& [name, text] : given) {
            if (name == input.name) value = text;
        }
        if (channels != 0U && !value.empty() && (value.front() == '#' || value.find(',') != std::string::npos)) {
            if (const std::optional<std::array<float, 4>> parsed = ParseColorText(value, channels)) {
                std::ostringstream joined;
                joined << (*parsed)[0] << ' ' << (*parsed)[1] << ' ' << (*parsed)[2];
                if (channels == 4U) joined << ' ' << (*parsed)[3];
                value = joined.str();
            }
        }
        const std::optional<std::uint32_t> constant = kb::render::AddRenderMaterialGraphNode(graph, kind, 60, static_cast<std::int32_t>(row++) * 120 + 200);
        if (!constant.has_value() || !kb::render::SetRenderMaterialGraphConstantValue(graph, *constant, value)) {
            io.err << "error: input " << input.name << " does not take '" << value << "' (" << TypeName(input.type) << ")\n";
            return 1;
        }
        static_cast<void>(kb::render::RenameRenderMaterialGraphNode(graph, *constant, input.name, "Constant"));
        if (!kb::render::ConnectRenderMaterialGraphPins(graph, *constant, pin, *callId, input.name)) {
            io.err << "error: input " << input.name << " cannot be wired to the call\n";
            return 1;
        }
    }

    const auto materialOutput = std::ranges::find_if(graph.nodes, [](const kb::render::RenderMaterialGraphNode& node) { return node.kind == Kind::MaterialOutput; });
    if (materialOutput == graph.nodes.end()) {
        io.err << "error: the material has no Material Output\n";
        return 1;
    }
    const std::vector<std::string> targets = kb::render::RenderMaterialGraphNodeInputPinNames(*materialOutput);
    const std::string targetWanted = arguments.Option("--to").value_or(output->type == RenderMaterialGraphPinType::Float2 ? "customizedUv0" : "emissive");
    const std::string target = PinOf(targets, targetWanted);
    if (target.empty()) {
        io.err << "error: the material output has no input '" << targetWanted << "' (inputs: " << JoinNames(targets) << ")\n";
        return 1;
    }
    const std::uint32_t outputId = materialOutput->id;
    if (!kb::render::ConnectRenderMaterialGraphPins(graph, *callId, output->name, outputId, target)) {
        io.err << "error: the function's " << output->name << " (" << TypeName(output->type) << ") cannot be connected to the material's " << target << '\n';
        return 1;
    }

    if (plain) {
        const float (&color)[4] = asset->desc.baseColor;
        const std::optional<std::uint32_t> base = kb::render::AddRenderMaterialGraphNode(graph, Kind::ConstantColor, 60, static_cast<std::int32_t>(row) * 120 + 200);
        const std::string value = std::to_string(color[0]) + " " + std::to_string(color[1]) + " " + std::to_string(color[2]) + " " + std::to_string(color[3]);
        if (base.has_value() && kb::render::SetRenderMaterialGraphConstantValue(graph, *base, value)) {
            static_cast<void>(kb::render::RenameRenderMaterialGraphNode(graph, *base, "Base Color", "Constant"));
            static_cast<void>(kb::render::ConnectRenderMaterialGraphPins(graph, *base, "rgba", outputId, "baseColor"));
        }
    }
    const Check check = CheckMaterial(*asset, scene.Assets().Manager().Registry());
    if (!check.loads || !check.problems.empty()) {
        io.err << "error: the material would not work with this function; nothing was written:\n";
        for (const std::string& problem : check.problems) io.err << "  " << problem << '\n';
        return 1;
    }
    if (!kb::render::RenderMaterialAssetWriter::Save(located->file, *asset)) {
        io.err << "error: the material could not be written: " << located->virtualPath << '\n';
        return 1;
    }
    io.out << located->virtualPath << " calls " << function->virtualPath << ": " << output->name << " -> " << target;
    for (const Endpoint& input : inputs) {
        std::string value = input.defaultValue;
        for (const auto& [name, text] : given) {
            if (name == input.name) value = text;
        }
        io.out << (&input == &inputs.front() ? " (" : ", ") << input.name << " = " << value;
    }
    io.out << (inputs.empty() ? "" : ")") << "\ncheck: kb_cli material-graph show --material " << located->virtualPath << " (Compiles: yes)\n";
    return 0;
}

} // namespace

int RunMaterialFunctionCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "new") return New(arguments, io);
    if (action == "show") return Show(arguments, io);
    if (action == "use") return Use(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
