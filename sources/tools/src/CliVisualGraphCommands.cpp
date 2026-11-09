#include "CliCommands.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/visual/VisualGraphAuthoringDocument.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"
#include "engine/visual/VisualGraphLiteral.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace kb::cli {
namespace {
using kb::core::JsonValue;
template<class T> std::optional<T> Number(const std::optional<std::string>& text) {
    if (!text || text->empty()) return std::nullopt;
    T value{}; const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) return std::nullopt;
    return value;
}
} // namespace
int RunVisualGraphCommand(const ArgumentList& arguments, CommandIo io) {
    using namespace kb::visual;
    const auto fail = [&](const std::string& error) { io.err << "error: " << error << '\n'; return 1; };
    if (arguments.Positionals().size() != 1) return fail("graph expects new|show|catalog|add|remove|move|literal|event|symbol|connect|disconnect|check --project <dir> --graph <path>.kbgraph");
    const auto& action = arguments.Positionals().front();
    const std::array<std::string_view,12> actions{"new","show","catalog","add","remove","move","literal","event","symbol","connect","disconnect","check"};
    if (std::ranges::find(actions, action) == actions.end()) return fail("Unknown graph operation");
    std::vector<std::string_view> allowed{"--project"};
    if (action != "catalog") allowed.push_back("--graph");
    if (action == "new") allowed.push_back("--name");
    if (action == "catalog") allowed.push_back("--filter");
    if (action == "add") allowed.insert(allowed.end(), {"--entry","--x","--y","--value"});
    if (action == "remove" || action == "move" || action == "literal" || action == "event" || action == "symbol") allowed.push_back("--node");
    if (action == "move") allowed.insert(allowed.end(), {"--x","--y"});
    if (action == "literal" || action == "event" || action == "symbol") allowed.push_back("--value");
    if (action == "connect") allowed.insert(allowed.end(), {"--from","--out","--to","--in"});
    if (action == "disconnect") allowed.push_back("--edge");
    if (!arguments.UnknownOptions(allowed).empty()) return fail("Option is not supported by this graph operation");
    const auto project = arguments.Option("--project");
    if (!project) return fail("graph requires --project <dir>");
    std::string error;
    std::error_code projectError;
    const auto projectPath = std::filesystem::weakly_canonical(kb::assets::Utf8Path(*project), projectError);
    if (projectError) return fail("Project path could not be resolved");
    const auto projectFile = kb::project::ProjectManager::FindProjectFile(projectPath, error);
    if (projectFile.empty()) return fail(error);
    const auto loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) return fail(loaded.error);
    kb::scene::Scene scene{loaded.descriptor};
    const auto root = projectFile.parent_path();
    if (!MountProjectAssets(scene, root, error)) return fail(error);
    kb::script::ScriptRuntimeHost host(scene);
    if (!host.Succeeded()) return fail("Graph authoring runtime could not initialize");
    const auto catalog = host.CreateVisualGraphNodeCatalog();
    auto result = JsonValue::MakeObject(); result.Set("operation", JsonValue::MakeString(action));
    if (action == "catalog") {
        auto entries = JsonValue::MakeArray(); const auto filter = arguments.Option("--filter").value_or("");
        for (const auto& entry : catalog.Entries()) {
            if (!filter.empty() && entry.id.find(filter) == std::string::npos && entry.displayName.find(filter) == std::string::npos) continue;
            auto row = JsonValue::MakeObject(); row.Set("id", JsonValue::MakeString(entry.id)); row.Set("name", JsonValue::MakeString(entry.displayName));
            row.Set("category", JsonValue::MakeString(entry.category)); auto pins = JsonValue::MakeArray();
            for (const auto& pin : entry.pins) {
                auto p = JsonValue::MakeObject(); p.Set("name", JsonValue::MakeString(pin.name)); p.Set("type", JsonValue::MakeString(ToString(pin.type)));
                p.Set("direction", JsonValue::MakeString(ToString(pin.direction))); pins.Append(std::move(p));
            }
            row.Set("pins", std::move(pins)); entries.Append(std::move(row));
        }
        result.Set("entries", std::move(entries)); io.out << result.Dump() << '\n'; return 0;
    }
    const auto reference = arguments.Option("--graph"); if (!reference) return fail("--graph <path>.kbgraph is required");
    auto input = kb::assets::Utf8Path(*reference);
    if (reference->starts_with("/Game/")) input = std::filesystem::path{"Assets"} / kb::assets::Utf8Path(reference->substr(6));
    if (!input.is_absolute()) input = root / input;
    const auto resolved = ResolveProjectFile(input, root, error); if (resolved.empty()) return fail(error);
    std::error_code fileError; const auto file = std::filesystem::weakly_canonical(resolved, fileError);
    const auto content = std::filesystem::weakly_canonical(root / "Assets", fileError);
    const auto relative = file.lexically_relative(content);
    if (fileError || relative.empty() || relative.is_absolute() || *relative.begin() == ".." || file.extension() != ".kbgraph")
        return fail("Expected a .kbgraph file inside the project's Assets directory");
    VisualGraphAuthoringDocument document;
    if (action == "new") {
        if (std::filesystem::exists(file)) return fail("Graph already exists");
        const auto name = file.stem().u8string();
        document.Create(arguments.Option("--name").value_or(std::string{reinterpret_cast<const char*>(name.data()), name.size()}));
    } else if (!document.Load(file, error)) return fail(error);
    const auto id = Number<std::uint32_t>(arguments.Option("--node"));
    std::uint32_t added = 0;
    if (action == "add") {
        const auto entry = arguments.Option("--entry"); const auto* found = entry ? catalog.Find(*entry) : nullptr;
        const auto x = arguments.Option("--x"), y = arguments.Option("--y");
        const auto px = x ? Number<float>(x) : std::optional<float>{32.0F}, py = y ? Number<float>(y) : std::optional<float>{32.0F};
        if (!found || !px || !py || !std::isfinite(*px) || !std::isfinite(*py)) return fail("add requires a catalog --entry and finite --x/--y");
        auto entryValue = *found;
        if (const auto value = arguments.Option("--value")) {
            if (entryValue.kind == VisualGraphNodeKind::Event) {
                if (!TryParseVisualGraphLifecycleEvent(*value, entryValue.lifecycle)) return fail("Unknown lifecycle event");
            } else if (VisualGraphLiteral::IsSymbol(entryValue.symbol)) {
                if (entryValue.pins.size() != 1 || !VisualGraphLiteral::Parse(entryValue.pins.front().type, *value)) return fail("Literal does not match its output type");
                entryValue.symbol = VisualGraphLiteral::Symbol(*value);
            } else if (entryValue.kind == VisualGraphNodeKind::CustomEvent || entryValue.kind == VisualGraphNodeKind::Comment) entryValue.symbol = *value;
            else return fail("--value is supported for literals, events and comments");
        }
        if (entryValue.kind == VisualGraphNodeKind::CustomEvent && entryValue.symbol.empty()) return fail("CustomEvent requires --value <name>");
        added = document.AddNode(entryValue, *px, *py, error); if (!added) return fail(error);
    } else if (action == "remove") { if (!id || !document.RemoveNode(*id, error)) return fail(error.empty() ? "remove requires a valid --node" : error);
    } else if (action == "move") {
        const auto x = Number<float>(arguments.Option("--x")), y = Number<float>(arguments.Option("--y"));
        if (!id || !x || !y || !document.MoveNode(*id, *x, *y, error)) return fail(error.empty() ? "move requires --node --x --y" : error);
    } else if (action == "literal" || action == "event" || action == "symbol") {
        const auto value = arguments.Option("--value"); if (!id || !value) return fail("--node and --value are required");
        bool edited = false;
        if (action == "literal") edited = document.SetLiteral(*id, *value, error);
        else if (action == "symbol") edited = document.SetSymbol(*id, *value, error);
        else { VisualGraphLifecycleEvent event; edited = TryParseVisualGraphLifecycleEvent(*value, event) && document.SetEvent(*id, event, error); }
        if (!edited) return fail(error.empty() ? "Invalid node or value" : error);
    } else if (action == "connect") {
        const auto from = Number<std::uint32_t>(arguments.Option("--from")), to = Number<std::uint32_t>(arguments.Option("--to"));
        const auto output = arguments.Option("--out"), target = arguments.Option("--in");
        if (!from || !to || !output || !target || !document.Connect(*from, *output, *to, *target, error))
            return fail(error.empty() ? "connect requires --from --out --to --in" : error);
    } else if (action == "disconnect") {
        const auto index = Number<std::size_t>(arguments.Option("--edge"));
        if (!index || !document.Disconnect(*index, error)) return fail(error.empty() ? "disconnect requires a valid --edge index" : error);
    }
    int code = 0;
    if (action == "check") {
        const auto compiled = VisualGraphCompiler::Compile(document.Snapshot().graph);
        auto diagnostics = compiled.diagnostics;
        if (compiled.Succeeded()) {
            const auto generated = VisualGraphNativeCodeGenerator::Generate(compiled.module, {.bindings = &host.VisualGraphNativeBindings(), .requireNativeBindings = true});
            diagnostics.insert(diagnostics.end(), generated.diagnostics.begin(), generated.diagnostics.end());
        }
        auto values = JsonValue::MakeArray();
        for (const auto& diagnostic : diagnostics) {
            auto value = JsonValue::MakeObject(); value.Set("node", JsonValue::MakeNumber(diagnostic.nodeId));
            value.Set("pin", JsonValue::MakeString(diagnostic.pinName)); value.Set("message", JsonValue::MakeString(diagnostic.message)); values.Append(std::move(value));
        }
        const bool valid = !VisualGraphDiagnostics::HasErrors(diagnostics); result.Set("valid", JsonValue::MakeBool(valid));
        result.Set("diagnostics", std::move(values)); code = valid ? 0 : 1;
    } else if (action != "show") {
        std::filesystem::create_directories(file.parent_path(), fileError);
        if (fileError || !document.Save(file, error)) return fail(error.empty() ? fileError.message() : error);
    }
    result.Set("nodeId", JsonValue::MakeNumber(added));
    auto nodes = JsonValue::MakeArray();
    for (const auto& node : document.Snapshot().graph.nodes) {
        auto value = JsonValue::MakeObject(); value.Set("id", JsonValue::MakeNumber(node.id)); value.Set("kind", JsonValue::MakeString(ToString(node.kind)));
        value.Set("symbol", JsonValue::MakeString(node.symbol)); value.Set("event", JsonValue::MakeString(ToString(node.lifecycle)));
        if (const auto text = VisualGraphLiteral::Text(node.symbol)) value.Set("value", JsonValue::MakeString(*text));
        for (const auto& point : document.Snapshot().positions) if (point.nodeId == node.id) { value.Set("x", JsonValue::MakeNumber(point.x)); value.Set("y", JsonValue::MakeNumber(point.y)); }
        nodes.Append(std::move(value));
    }
    result.Set("nodes", std::move(nodes)); auto edges = JsonValue::MakeArray();
    for (const auto& edge : document.Snapshot().graph.edges) {
        auto value = JsonValue::MakeObject(); value.Set("from", JsonValue::MakeNumber(edge.fromNode)); value.Set("out", JsonValue::MakeString(edge.fromPin));
        value.Set("to", JsonValue::MakeNumber(edge.toNode)); value.Set("in", JsonValue::MakeString(edge.toPin)); value.Set("kind", JsonValue::MakeString(ToString(edge.kind))); edges.Append(std::move(value));
    }
    result.Set("edges", std::move(edges)); io.out << result.Dump() << '\n'; return code;
}
} // namespace kb::cli
