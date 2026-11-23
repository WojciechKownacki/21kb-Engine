#include "CliCommands.hpp"
#include "CliCaptureImage.hpp"
#include "CliProgram.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/script/ScriptAgentProjectFiles.hpp"

#include <array>
#include <cstdio>
#include <cmath>
#include <exception>
#include <limits>
#include <initializer_list>
#include <set>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::cli {
using kb::core::JsonValue;

namespace {

// The versions the server speaks: a client that asks for one of them gets it, any other client the newest. Tool annotations
// (readOnlyHint, destructiveHint) are part of the newer one; a client of the older one ignores the field.
constexpr std::string_view kProtocolVersions[]{ "2024-11-05", "2025-03-26" };
constexpr std::string_view kServerVersion = "0.1.0";

// The tools an agent gets: kb_cli, guide and api_reference by default (every other command is a kb_cli command, and a tool of its
// own would be sent again with every step), all of them with `kb_cli mcp --tools all`.
bool gAllTools = false;
// A capture comes back as a picture too (an image block), unless `kb_cli mcp --images off`: for a client that takes text only.
bool gImages = true;
// The widest side a picture is sent at: wide enough to read a scene, small enough to be cheap in tokens.
constexpr unsigned kPictureWidth = 640U;
constexpr std::string_view kCodeActions[]{"modules", "map", "find", "show"};
constexpr std::string_view kEntityActions[]{"add", "set", "remove", "rename", "attach", "detach", "show"};
constexpr std::string_view kComponentActions[]{"add", "set", "remove"};
constexpr std::string_view kSceneTemplates[]{"Basic3D", "Basic2D", "FirstPerson", "ThirdPerson", "TopDown", "Platformer2D", "Vehicle", "Empty", "Basic"};
constexpr std::string_view kYesNo[]{"yes", "no"};
constexpr std::string_view kScriptLanguages[]{"lua", "cpp"};
constexpr std::string_view kBlendSpaceActions[]{"new", "show", "add", "set", "remove", "sample", "assign"};
constexpr std::string_view kGraphActions[]{"new","show","catalog","add","remove","move","literal","event","symbol","connect","disconnect","check"};

struct ToolProperty {
    std::string_view name;
    std::string_view type;
    std::string_view description;
    bool required = false;
    std::string_view cliOption;
    std::span<const std::string_view> choices{};
};

// tools/list is sent with every agent request, so what it shows leaves out what only the server
// uses: the CLI option each argument becomes and the range checks it applies itself.
thread_local bool tDiscovery = false;

[[nodiscard]] JsonValue MakeToolDescriptor(
    std::string_view name,
    std::string_view description,
    std::initializer_list<ToolProperty> properties) {
    JsonValue schema = JsonValue::MakeObject();
    schema.Set("type", JsonValue::MakeString("object"));
    schema.Set("additionalProperties", JsonValue::MakeBool(false));
    JsonValue schemaProperties = JsonValue::MakeObject();
    JsonValue required = JsonValue::MakeArray();
    for (const ToolProperty& property : properties) {
        JsonValue descriptor = JsonValue::MakeObject();
        descriptor.Set("type", JsonValue::MakeString(std::string{ property.type }));
        descriptor.Set("description", JsonValue::MakeString(std::string{ property.description }));
        if (!tDiscovery) descriptor.Set("x-cli-option", JsonValue::MakeString(std::string{property.cliOption}));
        if (property.choices.size() != 0U) {
            auto choices = JsonValue::MakeArray();
            for (const auto choice : property.choices) choices.Append(JsonValue::MakeString(std::string{choice}));
            descriptor.Set("enum", std::move(choices));
        }
        if (property.name == "topic") descriptor.Set("default", JsonValue::MakeString("index"));
        if (property.name == "inputReplay" || property.name == "inputRecord")
            descriptor.Set("x-project-file", JsonValue::MakeBool(true));
        if (property.type == "array" || property.type == "lists") {
            auto items = JsonValue::MakeObject();
            items.Set("type", JsonValue::MakeString("string"));
            items.Set("minLength", JsonValue::MakeNumber(1));
            if (property.type == "lists") {
                auto list = JsonValue::MakeObject();
                list.Set("type", JsonValue::MakeString("array"));
                list.Set("items", std::move(items));
                items = std::move(list);
                descriptor.Set("type", JsonValue::MakeString("array"));
            }
            descriptor.Set("items", std::move(items));
        }
        if (property.type == "string" && !tDiscovery) descriptor.Set("minLength", JsonValue::MakeNumber(property.name == "value" ? 0 : 1));
        if (property.type == "integer" && !tDiscovery) {
            descriptor.Set("minimum", JsonValue::MakeNumber(property.name == "executionOrder" ? std::numeric_limits<int>::min() : property.name == "index" || property.name == "edge" ? 0 : 1));
            descriptor.Set("maximum", JsonValue::MakeNumber(property.name == "index" ? 255 : std::numeric_limits<int>::max()));
        }
        schemaProperties.Set(std::string{ property.name }, std::move(descriptor));
        if (property.required) {
            required.Append(JsonValue::MakeString(std::string{ property.name }));
        }
    }
    schema.Set("properties", std::move(schemaProperties));
    if (required.Size() > 0U) {
        schema.Set("required", std::move(required));
    }

    JsonValue tool = JsonValue::MakeObject();
    tool.Set("name", JsonValue::MakeString(std::string{ name }));
    tool.Set("description", JsonValue::MakeString(std::string{ description }));
    tool.Set("inputSchema", std::move(schema));
    return tool;
}

[[nodiscard]] JsonValue MakeCommandToolDescriptor(const CliCommandDescriptor& command) {
    if (command.name == "graph") return MakeToolDescriptor(command.mcpTool,
        "Authors gameplay graphs through the shared validated document model and atomic saves; checks provider signatures.", {
            ToolProperty{"action","string","Graph operation; catalog enumerates node IDs and typed pins.",true,"",kGraphActions},
            ToolProperty{"graph","string","A .kbgraph path inside the project Assets directory, required except for catalog.",false,"--graph"},
            ToolProperty{"filter","string","Catalog search text.",false,"--filter"},
            ToolProperty{"name","string","Name for a new graph.",false,"--name"},
            ToolProperty{"entry","string","Catalog ID to add.",false,"--entry"},
            ToolProperty{"node","integer","Node to remove, move or edit.",false,"--node"},
            ToolProperty{"x","number","Finite node canvas coordinate.",false,"--x"},
            ToolProperty{"y","number","Finite node canvas coordinate.",false,"--y"},
            ToolProperty{"value","string","Literal text, lifecycle event or custom node name.",false,"--value"},
            ToolProperty{"from","integer","Source node ID.",false,"--from"},
            ToolProperty{"out","string","Source output pin name.",false,"--out"},
            ToolProperty{"to","integer","Target node ID.",false,"--to"},
            ToolProperty{"in","string","Target input pin name.",false,"--in"},
            ToolProperty{"edge","integer","Zero-based edge index to disconnect.",false,"--edge"},
        });
    if (command.name == "blend-space") return MakeToolDescriptor(command.mcpTool,
        "Authors and samples a BlendSpace2D asset with atomic validated edits, or assigns it to a controller state.", {
            ToolProperty{"action","string","Operation; add requires at/clip; set/remove require index; assign requires controller/state/axis names.",true,"",kBlendSpaceActions},
            ToolProperty{"space","string","BlendSpace2D asset path inside the project Assets directory.",true,"--space"},
            ToolProperty{"at","string","Two finite coordinates x,y for add/set/sample.",false,"--at"},
            ToolProperty{"clip","string","AnimationClip virtual path, physical asset path or stable identity.",false,"--clip"},
            ToolProperty{"index","integer","Zero-based sample index for set/remove.",false,"--index"},
            ToolProperty{"controller","string","Animator Controller to edit for assign.",false,"--controller"},
            ToolProperty{"state","string","Existing controller state to replace for assign.",false,"--state"},
            ToolProperty{"layer","string","Controller layer, defaulting to its first layer.",false,"--layer"},
            ToolProperty{"xParameter","string","Float parameter bound to the X axis; created if missing.",false,"--x-param"},
            ToolProperty{"yParameter","string","Distinct Float parameter bound to the Y axis; created if missing.",false,"--y-param"},
        });
    if (command.name == "api") return MakeToolDescriptor(command.mcpTool,
        "Returns one page of the script API: topic a group (Physics), a function (Physics.Raycast), a component (Rigidbody), a global (Log, Emit) "
        "or an event hook (OnTriggerEnter; EventHooks lists them); search the topics matching words; neither, the index. Ask for the one you are about to use.",
        {
            ToolProperty{ "topic", "string", "A group, function or component name, or 'index'.", false, "--topic" },
            ToolProperty{ "search", "string", "Words to find in function and component names and descriptions.", false, "--search" },
        });
    if (command.name == "guide") return MakeToolDescriptor(command.mcpTool,
        "Returns a short how-to with the commands that do the job and the check that proves it. Start with the recipe for the job (AGENTS.md names them); "
        "without a topic, one line per guide; a wrong topic returns the closest ones.",
        {
            ToolProperty{ "topic", "string", "The guide's name, for example recipe-material.", false, "--topic" },
        });
    if (command.name == "code") return MakeToolDescriptor(command.mcpTool,
        "Finds your way in the engine's own source without reading all of it: modules returns the code map as JSON (every module and "
        "subsystem with its responsibility, entry points, build and test targets, ctest pattern and dependencies; topic keeps the entries "
        "about those words), map lists folders with their size and purpose "
        "(and the files of one folder), find names the files that deal with a topic and the line to start at, show prints where a "
        "type or function is declared, its comment, its declaration, the files that use it and its tests.",
        {
            ToolProperty{ "action", "string", "modules, map, find or show.", true, "", kCodeActions },
            ToolProperty{ "topic", "string", "Words the returned entries must be about, for example shadow or navmesh (modules).", false, "--topic" },
            ToolProperty{ "path", "string", "Folder to map or to search in, for example sources/renderer.", false, "--path" },
            ToolProperty{ "depth", "string", "How many folder levels map prints (1-8).", false, "--depth" },
            ToolProperty{ "text", "string", "Words every found file must mention (find).", false, "--text" },
            ToolProperty{ "symbol", "string", "Exact name of a type or function (show).", false, "--symbol" },
            ToolProperty{ "limit", "string", "How many files find lists, or entries modules returns.", false, "--limit" },
        });
    if (command.name == "import") return MakeToolDescriptor(command.mcpTool,
        "Imports an external source asset into a project's runtime asset format and registers it.",
        {
            ToolProperty{ "source", "string", "Physical source file path, or a path relative to the project root.", true, "" },
            ToolProperty{ "destination", "string", "Destination virtual folder, for example /Game/Fonts.", true, "--destination" },
        });
    if (command.name == "validate") return MakeToolDescriptor(command.mcpTool,
        "Validates a Lua behaviour script (syntax and sandbox load) and reports errors with line numbers; for a .21kbscene it names UI mistakes "
        "(widget outside a Canvas, Text without a font, event name or Button nothing answers).",
        {
            ToolProperty{ "path", "string", "Path to the .lua or .21kbscene file, relative to the project root.", true, "" },
        });
    if (command.name == "scene-list") return MakeToolDescriptor(command.mcpTool,
        "Lists the nodes of a .21kbscene file with their components, behaviours and world positions (@ x,y,z).",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...); defaults to the startup scene.", false, "--scene" },
        });
    if (command.name == "scene-attach") return MakeToolDescriptor(command.mcpTool,
        "Attaches a behaviour script asset to a named scene node and saves the scene.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...); defaults to the startup scene.", false, "--scene" },
            ToolProperty{ "node", "string", "Name of the scene node to attach the behaviour to.", true, "--node" },
            ToolProperty{ "script", "string", "Script asset path: /Game/... virtual path or a path under Assets/.", true, "--script" },
            ToolProperty{ "tickGroup", "string", "Optional tick group: Input, Gameplay, Physics, Animation, Camera, Presentation.", false, "--tick-group" },
            ToolProperty{ "executionOrder", "integer", "Optional execution order within the tick group.", false, "--execution-order" },
            ToolProperty{ "set", "string", "Optional: one exposed script variable for this node, as name=value.", false, "--set" },
        });
    if (command.name == "run") return MakeToolDescriptor(command.mcpTool,
        "Runs a scene headless for N frames and reports script logs, events, errors and where dynamic bodies moved.",
        {
            ToolProperty{ "scene", "string", "Scene path; defaults to the startup scene.", false, "--scene" },
            ToolProperty{ "frames", "integer", "Frame count (default 60).", false, "--frames" },
            ToolProperty{ "press", "string", "Hold a key during the run to test input: Space@30 (frame 30) or E@10+20 (20 frames).", false, "--press" },
            ToolProperty{ "dt", "number", "Delta seconds per frame (default 1/60).", false, "--dt" },
            ToolProperty{ "report", "string", "A versioned report: particles, input, audio, ai or ecs.", false, "--report" },
            ToolProperty{ "inputReplay", "string", "Recording file to replay with its per-tick durations.", false, "--input-replay" },
            ToolProperty{ "inputRecord", "string", "Recording file to write with input consumed by completed ticks.", false, "--input-record" },
            ToolProperty{ "viewport", "string", "Viewport WIDTHxHEIGHT in pixels; overrides recorded viewport sizes.", false, "--viewport" },
            ToolProperty{ "safeArea", "string", "Nonnegative LEFT,TOP,RIGHT,BOTTOM safe-area insets in viewport pixels.", false, "--safe-area" },
        });
    if (command.name == "schema") return MakeToolDescriptor(command.mcpTool,
        "Lists one component's properties when component is given; without it, every component and the create presets.", {
            ToolProperty{ "component", "string", "A component name, for example Rigidbody or MeshRenderer.", false, "--component" },
        });
    if (command.name == "entity") return MakeToolDescriptor(command.mcpTool,
        "Creates, edits and shows entities. add with create, color and body makes a coloured shape with physics in one call; "
        "set moves, turns or aims one (lookAt); show prints an entity's transform and every component value.", {
            ToolProperty{"action", "string", "Entity operation.", true, "", kEntityActions},
            ToolProperty{"name", "string", "Entity name or hierarchy path (add: defaults to the created item).", false, "--name"},
            ToolProperty{"scene", "string", "Scene path; defaults to the startup scene.", false, "--scene"},
            ToolProperty{"create", "string", "Optional built-in shape or player preset.", false, "--create"},
            ToolProperty{"parent", "string", "Optional parent entity.", false, "--parent"},
            ToolProperty{"to", "string", "Rename target or socket attachment owner.", false, "--to"},
            ToolProperty{"socket", "string", "Socket or bone name.", false, "--socket"},
            ToolProperty{"position", "string", "Position x,y,z.", false, "--position"},
            ToolProperty{"rotation", "string", "Rotation in degrees x,y,z.", false, "--rotation"},
            ToolProperty{"scale", "string", "Scale x,y,z.", false, "--scale"},
            ToolProperty{"lookAt", "string", "Turn forward (+Z) toward a point x,y,z or an entity, e.g. a camera at the player.", false, "--look-at"},
            ToolProperty{"color", "string", "add: a colour #RRGGBB for a new material named after the entity.", false, "--color"},
            ToolProperty{"material", "string", "add: an existing material, /Game/....kbmat.", false, "--material"},
            ToolProperty{"body", "string", "add: a Rigidbody of this type (Static, Dynamic, Kinematic).", false, "--body"},
            ToolProperty{"body2d", "string", "add: 2D physics, a Body 2D of this type (Static, Kinematic, Dynamic) and a Collider 2D following the scale.", false, "--body2d"},
            ToolProperty{"count", "integer", "add: this many entities Name1..NameN, each offset further (a stack, a row).", false, "--count"},
            ToolProperty{"offset", "string", "add with count: the step between them x,y,z (default 0,1,0).", false, "--offset"},
            ToolProperty{"occurrence", "integer", "One-based index when names are ambiguous.", false, "--occurrence"},
        });
    if (command.name == "component") return MakeToolDescriptor(command.mcpTool,
        "Adds, edits or removes a component with the CLI authoring and validation rules.", {
            ToolProperty{"action", "string", "Component operation.", true, "", kComponentActions},
            ToolProperty{"entity", "string", "Entity name or hierarchy path.", true, "--entity"},
            ToolProperty{"type", "string", "Component type from component_schema.", true, "--type"},
            ToolProperty{"scene", "string", "Scene path; defaults to the startup scene.", false, "--scene"},
            ToolProperty{"set", "array", "Repeated property=value assignments.", false, "--set"},
        });
    if (command.name == "scene-new") return MakeToolDescriptor(command.mcpTool,
        "Creates a scene from an engine template.", {
            ToolProperty{"name", "string", "New scene name.", true, "--name"},
            ToolProperty{"folder", "string", "Project asset folder.", false, "--folder"},
            ToolProperty{"template", "string", "Scene template.", false, "--template", kSceneTemplates},
            ToolProperty{"default", "string", "Make this the startup scene.", false, "--default", kYesNo},
        });
    if (command.name == "script-new") return MakeToolDescriptor(command.mcpTool,
        "Creates a Lua or native script using the engine templates.", {
            ToolProperty{"name", "string", "New script name.", true, "--name"},
            ToolProperty{"language", "string", "Script language.", true, "--language", kScriptLanguages},
            ToolProperty{"folder", "string", "Project asset folder.", false, "--folder"},
            ToolProperty{"code", "string", "Lua only: the script's whole text, written instead of the template.", false, "--code"},
        });
    return JsonValue::MakeNull();
}

// Runs any kb_cli command for the bound project, so an agent reaches every operation the guides
// describe (materials, animation, maps, abilities) and not only the ones with a tool of their own.
constexpr std::string_view kCliToolName = "kb_cli";

[[nodiscard]] JsonValue MakeCliToolDescriptor() {
    return MakeToolDescriptor(kCliToolName,
        "Runs kb_cli commands for this project; the project is filled in. `args` is one command: its words, one per item, for example "
        "[\"material-new\",\"--name\",\"Red\",\"--set\",\"Base Color=#E03C31\"]. `commands` is several, a list of such lists, run in order as one "
        "transaction: if one fails every file is put back. [\"help\"] lists the commands, [\"help\",\"<command>\"] shows one's options.",
        {
            ToolProperty{ "args", "array", "One command and its options, one word per item.", false, "" },
            ToolProperty{ "commands", "lists", "Several commands, each a list of words, run as one all-or-nothing transaction.", false, "" },
        });
}

// What a client may rely on: a tool that only reads says so, and none of these tools changes anything outside the project or
// deletes for good (an asset removed goes to the Recycle Bin).
void Annotate(JsonValue& tool) {
    const std::string name = tool.Find("name")->AsString();
    const bool readOnly = name == "guide" || name == "api_reference" || name == "code" || name == "component_schema" || name == "scene_list" || name == "validate_script";
    JsonValue annotations = JsonValue::MakeObject();
    annotations.Set("readOnlyHint", JsonValue::MakeBool(readOnly));
    if (!readOnly) annotations.Set("destructiveHint", JsonValue::MakeBool(false));
    annotations.Set("openWorldHint", JsonValue::MakeBool(false));
    tool.Set("annotations", std::move(annotations));
}

[[nodiscard]] JsonValue MakeToolList() {
    auto tools = JsonValue::MakeArray();
    const auto add = [&tools](JsonValue tool) {
        Annotate(tool);
        tools.Append(std::move(tool));
    };
    add(MakeCliToolDescriptor());
    for (const auto& command : CliCommandCatalog()) {
        const bool core = command.name == "guide" || command.name == "api";
        if (command.available && !command.mcpTool.empty() && (core || gAllTools)) add(MakeCommandToolDescriptor(command));
    }
    return tools;
}

void WriteMessage(std::ostream& out, const JsonValue& message) {
    out << ToValidUtf8(message.Dump()) << '\n';
    out.flush();
}

void WriteResult(std::ostream& out, const JsonValue& id, JsonValue result) {
    JsonValue message = JsonValue::MakeObject();
    message.Set("jsonrpc", JsonValue::MakeString("2.0"));
    message.Set("id", id);
    message.Set("result", std::move(result));
    WriteMessage(out, message);
}

void WriteError(std::ostream& out, const JsonValue& id, int code, std::string_view text) {
    JsonValue error = JsonValue::MakeObject();
    error.Set("code", JsonValue::MakeNumber(static_cast<double>(code)));
    error.Set("message", JsonValue::MakeString(std::string{ text }));
    JsonValue message = JsonValue::MakeObject();
    message.Set("jsonrpc", JsonValue::MakeString("2.0"));
    message.Set("id", id);
    message.Set("error", std::move(error));
    WriteMessage(out, message);
}

[[nodiscard]] std::string GetStringArgument(const JsonValue* toolArguments, std::string_view key) {
    if (toolArguments == nullptr) {
        return {};
    }
    const JsonValue* value = toolArguments->Find(key);
    return value != nullptr ? value->AsString() : std::string{};
}

struct ToolCallOutcome {
    bool isError = false;
    std::string text;
    // Picture files a command wrote (captures), sent back as image blocks under the text.
    std::vector<std::filesystem::path> pictures;
};

// A capture that names no file gets one inside the project, so the picture can be handed back. Returns the file the capture
// writes (a relative --out is the project's), or an empty path when the words are no capture.
[[nodiscard]] std::filesystem::path PrepareCapture(std::vector<std::string>& words, const std::string& projectRoot, std::set<std::filesystem::path>& taken) {
    if (words.empty() || words.front() != "capture" || projectRoot.empty()) return {};
    const std::filesystem::path root = std::filesystem::absolute(std::filesystem::path{ projectRoot }).lexically_normal();
    std::filesystem::path picture;
    for (std::size_t index = 1U; index + 1U < words.size(); ++index) {
        if (words[index] != "--out") continue;
        picture = std::filesystem::path{ words[index + 1U] };
        break;
    }
    if (picture.empty()) {
        picture = NextCapturePath(root, taken);
        // The project's own relative path: what the agent reads back names no folder of this machine.
        words.push_back("--out");
        words.push_back(picture.lexically_relative(root).generic_string());
    }
    if (picture.is_relative()) picture = root / picture;
    taken.insert(picture.lexically_normal());
    return picture;
}

// Enforce the same schema that discovery advertises before translating values
// into CLI arguments, especially before narrowing JSON numbers to native ints.
[[nodiscard]] std::string ValidateToolArguments(std::string_view name, const JsonValue* arguments) {
    const auto tools = MakeToolList();
    for (std::size_t index = 0U; index < tools.Size(); ++index) {
        const auto* tool = tools.At(index);
        if (tool->Find("name")->AsString() != name) continue;
        const auto* schema = tool->Find("inputSchema");
        const auto* properties = schema->Find("properties");
        if (arguments != nullptr && arguments->GetKind() != JsonValue::Kind::Object)
            return "arguments must be an object";
        if (const auto* required = schema->Find("required")) {
            for (std::size_t item = 0U; item < required->Size(); ++item) {
                const auto& key = required->At(item)->AsString();
                if (arguments == nullptr || arguments->Find(key) == nullptr) return "missing required argument: " + key;
            }
        }
        for (std::size_t item = 0U; arguments != nullptr && item < arguments->Size(); ++item) {
            const auto& key = arguments->MemberName(item);
            const auto* property = properties->Find(key);
            if (property == nullptr) return "unknown argument: " + key;
            const auto* value = arguments->Find(key);
            const auto& type = property->Find("type")->AsString();
            if (type == "string") {
                if (value->GetKind() != JsonValue::Kind::String ||
                    (value->AsString().empty() && property->Find("minLength")->AsNumber() > 0))
                    return key + " must be a nonempty string";
                if (const auto* choices = property->Find("enum")) {
                    bool accepted = false;
                    for (std::size_t choice = 0U; choice < choices->Size(); ++choice)
                        accepted = accepted || choices->At(choice)->AsString() == value->AsString();
                    if (!accepted) return key + " is not one of the advertised values";
                }
            } else if (type == "array") {
                const bool nested = property->Find("items")->Find("type")->AsString() == "array";
                if (value->GetKind() != JsonValue::Kind::Array) return key + (nested ? " must be a list of lists of strings" : " must be an array of strings");
                const auto nonempty = [](const JsonValue* word) { return word->GetKind() == JsonValue::Kind::String && !word->AsString().empty(); };
                for (std::size_t element = 0U; element < value->Size(); ++element) {
                    const JsonValue* entry = value->At(element);
                    if (!nested && !nonempty(entry)) return key + " must contain nonempty strings";
                    if (!nested) continue;
                    if (entry->GetKind() != JsonValue::Kind::Array || entry->Size() == 0U) return key + " must be a list of nonempty lists of strings";
                    for (std::size_t word = 0U; word < entry->Size(); ++word)
                        if (!nonempty(entry->At(word))) return key + " must contain nonempty strings";
                }
            } else {
                const double number = value->AsNumber();
                if (value->GetKind() != JsonValue::Kind::Number || !std::isfinite(number))
                    return key + " must be a finite number";
                if (type == "integer" && (std::trunc(number) != number ||
                    number < property->Find("minimum")->AsNumber() || number > property->Find("maximum")->AsNumber()))
                    return key + " must be an integer within the advertised range";
            }
        }
        return {};
    }
    return "unknown tool: " + std::string{name};
}

// The words of one command of the kb_cli tool: a leading "kb_cli" and a --project (the project is the bound one) are dropped.
[[nodiscard]] std::vector<std::string> WordsOf(const JsonValue& list) {
    std::vector<std::string> words;
    for (std::size_t index = 0U; index < list.Size(); ++index) {
        const std::string& word = list.At(index)->AsString();
        if (word == "kb_cli" && words.empty()) continue;
        if (word == "--project") {
            ++index;
            continue;
        }
        words.push_back(word);
    }
    return words;
}

// True when the project's AGENTS.md opens with the rules the server would send (an older or edited file does not).
[[nodiscard]] bool ProjectFileHoldsRules(const std::string& projectRoot) {
    if (projectRoot.empty()) return false;
    std::ifstream file(std::filesystem::path{ projectRoot } / "AGENTS.md", std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    std::erase(text, '');
    return text.find(kb::script::ScriptAgentProjectFiles::AgentRules()) != std::string::npos;
}

// Several commands as one transaction on the bound project (the batch command's: all of them take effect, or none), with
// the output of each. A scene a command changed is on disk when the next one runs.
[[nodiscard]] ToolCallOutcome RunCommandList(const JsonValue& commands, const std::string& projectRoot) {
    if (projectRoot.empty()) return {.isError = true, .text = "commands needs a bound project"};
    std::vector<std::vector<std::string>> operations;
    std::vector<std::filesystem::path> pictures;
    std::set<std::filesystem::path> taken;
    for (std::size_t index = 0U; index < commands.Size(); ++index) {
        std::vector<std::string> words = WordsOf(*commands.At(index));
        if (words.empty()) return {.isError = true, .text = "commands[" + std::to_string(index) + "] must name a command"};
        if (std::filesystem::path picture = PrepareCapture(words, projectRoot, taken); !picture.empty()) pictures.push_back(std::move(picture));
        operations.push_back(std::move(words));
    }
    if (operations.empty()) return {.isError = true, .text = "commands must hold at least one command"};
    std::ostringstream captured;
    std::istringstream noInput;
    int exitCode = 1;
    try {
        exitCode = RunBatchOperations(projectRoot, std::move(operations), false, false, true, {.out = captured, .err = captured},
            [&noInput](std::span<const std::string> words, CommandIo target) { return RunCli(words, noInput, target); });
    } catch (const std::exception& error) {
        captured << "error: " << error.what() << '\n';
    } catch (...) {
        captured << "error: unexpected tool failure\n";
    }
    ToolCallOutcome outcome{.isError = exitCode != 0, .text = captured.str()};
    if (!outcome.isError) outcome.pictures = std::move(pictures);
    return outcome;
}

// What a command that failed was told, and how often in a row: a command that fails the same way again is answered with one
// short hint instead of the whole error, so an agent that loops on it is turned round. A success forgets it.
struct RepeatedFailure {
    std::string text;
    int count = 0;
};

void NoteRepeatedFailure(std::map<std::string, RepeatedFailure>& memory, const std::string& key, ToolCallOutcome& outcome) {
    if (!outcome.isError) {
        memory.erase(key);
        return;
    }
    RepeatedFailure& seen = memory[key];
    if (seen.count > 0 && seen.text == outcome.text) {
        ++seen.count;
        std::string firstLine = seen.text.substr(0U, seen.text.find('\n'));
        if (firstLine.size() > 200U) firstLine.resize(200U);
        outcome.text = "This failed " + std::to_string(seen.count) + " times with the same arguments. Change the arguments, or stop and report the problem.\nThe error: " + firstLine;
        return;
    }
    seen = { outcome.text, 1 };
}

[[nodiscard]] ToolCallOutcome DispatchTool(
    std::string_view toolName,
    const JsonValue* toolArguments,
    const std::string& projectRoot) {
    if (const auto error = ValidateToolArguments(toolName, toolArguments); !error.empty())
        return {.isError = true, .text = error};
    if (toolName == kCliToolName) {
        const JsonValue* args = toolArguments != nullptr ? toolArguments->Find("args") : nullptr;
        const JsonValue* commands = toolArguments != nullptr ? toolArguments->Find("commands") : nullptr;
        if ((args == nullptr) == (commands == nullptr)) return {.isError = true, .text = "give either args (one command) or commands (a list of commands)"};
        if (commands != nullptr) return RunCommandList(*commands, projectRoot);
        std::vector<std::string> words = WordsOf(*args);
        if (words.empty()) return {.isError = true, .text = "args must name a command"};
        const CliCommandDescriptor* target = FindCliCommand(words.front());
        // An unknown command goes to the program, which names the nearest one.
        if (target != nullptr && target->json == CliJsonOutput::Protocol) return {.isError = true, .text = "a protocol server (mcp, lsp, dap) cannot run inside the MCP server"};
        // Commands about the project get it (a batch works on the bound project too); the ones that make a project or
        // describe kb_cli do not take it.
        const bool takesProject = target != nullptr && !projectRoot.empty() &&
            (target->kind == CliCommandKind::Normal || target->kind == CliCommandKind::Particle || target->kind == CliCommandKind::Batch) &&
            words.front() != "new";
        std::set<std::filesystem::path> taken;
        const std::filesystem::path picture = PrepareCapture(words, projectRoot, taken);
        if (takesProject) words.insert(words.begin() + 1, { "--project", projectRoot });
        std::ostringstream captured;
        std::istringstream noInput;
        int exitCode = 1;
        try {
            exitCode = RunCli(words, noInput, {.out = captured, .err = captured});
        } catch (const std::exception& error) {
            captured << "error: " << error.what() << '\n';
        } catch (...) {
            captured << "error: unexpected tool failure\n";
        }
        ToolCallOutcome outcome{.isError = exitCode != 0, .text = captured.str()};
        if (!outcome.isError && !picture.empty()) outcome.pictures.push_back(picture);
        return outcome;
    }
    const CliCommandDescriptor* command = nullptr;
    for (const auto& entry : CliCommandCatalog())
        if (entry.available && entry.mcpTool == toolName) { command = &entry; break; }
    if (command == nullptr) return {.isError = true, .text = "unknown tool: " + std::string{toolName}};
    if (projectRoot.empty() && command->name != "api" && command->name != "schema" && command->name != "guide" && command->name != "code")
        return {.isError = true, .text = "this tool requires a bound project"};
    std::vector<std::string> words{std::string{command->name}};
    if (!projectRoot.empty() && command->name != "guide" && command->name != "code") words.insert(words.end(), {"--project", projectRoot});
    const auto descriptor = MakeCommandToolDescriptor(*command);
    const auto* properties = descriptor.Find("inputSchema")->Find("properties");
    for (std::size_t index = 0U; index < properties->Size(); ++index) {
        const auto& name = properties->MemberName(index);
        const auto* property = properties->Find(name);
        const auto* value = toolArguments == nullptr ? nullptr : toolArguments->Find(name);
        if (value == nullptr) value = property->Find("default");
        if (value == nullptr) continue;
        const auto& option = property->Find("x-cli-option")->AsString();
        const auto append = [&](std::string text) {
            if (!option.empty()) words.push_back(option);
            words.push_back(std::move(text));
        };
        if (value->GetKind() == JsonValue::Kind::Array) {
            for (std::size_t item = 0U; item < value->Size(); ++item) append(value->At(item)->AsString());
            continue;
        }
        std::string text;
        if (value->GetKind() == JsonValue::Kind::String) text = value->AsString();
        else if (property->Find("type")->AsString() == "integer") text = std::to_string(static_cast<int>(value->AsNumber()));
        else {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", value->AsNumber());
            text = buffer;
        }
        if (const auto* confined = property->Find("x-project-file"); confined != nullptr && confined->AsBool()) {
            std::string error;
            std::error_code pathError;
            const auto input = std::filesystem::path{text};
            const auto anchored = std::filesystem::absolute(input.is_absolute() ? input : std::filesystem::path{projectRoot} / input, pathError);
            if (pathError) return {.isError = true, .text = "cannot resolve recording path: " + pathError.message()};
            const auto resolved = ResolveProjectFile(anchored, std::filesystem::path{projectRoot}, error);
            if (resolved.empty()) return {.isError = true, .text = error};
            text = resolved.string();
        }
        append(std::move(text));
    }
    std::ostringstream captured;
    std::ostringstream diagnostics;
    std::istringstream noInput;
    const bool structuredReport = command->name == "run" && !GetStringArgument(toolArguments, "report").empty();
    int exitCode = 1;
    try {
        exitCode = RunCli(words, noInput, {.out = captured, .err = structuredReport ? diagnostics : captured});
    } catch (const std::exception& error) {
        diagnostics << "error: " << error.what() << '\n';
    } catch (...) {
        diagnostics << "error: unexpected tool failure\n";
    }
    if (exitCode != 0) captured << diagnostics.str();
    return {.isError = exitCode != 0, .text = captured.str()};
}

} // namespace

int RunMcpCommand(const ArgumentList& arguments, std::istream& in, CommandIo io) {
    const std::string projectRoot = arguments.Option("--project").value_or("");
    gAllTools = arguments.Option("--tools").value_or("core") == "all";
    gImages = arguments.Option("--images").value_or("on") != "off";
    // An agent works through this server: everything it runs stays inside its project.
    ConfineCliToProject(projectRoot);

    std::map<std::string, RepeatedFailure> failures;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }

        JsonValue request;
        std::string parseError;
        if (!JsonValue::Parse(line, request, parseError)) {
            WriteError(io.out, JsonValue::MakeNull(), -32700, "parse error: " + parseError);
            continue;
        }

        const JsonValue* method = request.Find("method");
        const JsonValue* id = request.Find("id");
        const bool isNotification = id == nullptr || id->IsNull();
        const std::string methodName = method != nullptr ? method->AsString() : std::string{};

        if (methodName == "initialize") {
            JsonValue capabilities = JsonValue::MakeObject();
            capabilities.Set("tools", JsonValue::MakeObject());
            JsonValue serverInfo = JsonValue::MakeObject();
            serverInfo.Set("name", JsonValue::MakeString("kb-engine"));
            serverInfo.Set("version", JsonValue::MakeString(std::string{ kServerVersion }));
            const JsonValue* initializeParams = request.Find("params");
            const JsonValue* requested = initializeParams != nullptr ? initializeParams->Find("protocolVersion") : nullptr;
            std::string_view version = kProtocolVersions[std::size(kProtocolVersions) - 1U];
            for (const std::string_view supported : kProtocolVersions)
                if (requested != nullptr && requested->AsString() == supported) version = supported;
            JsonValue result = JsonValue::MakeObject();
            result.Set("protocolVersion", JsonValue::MakeString(std::string{ version }));
            result.Set("capabilities", std::move(capabilities));
            result.Set("serverInfo", std::move(serverInfo));
            // The rules of working here, for a CLI that reads no instruction file of the project: one that does has them in its
            // context already (AGENTS.md opens with them), and sending them again would cost every step the same text twice.
            if (!ProjectFileHoldsRules(projectRoot)) result.Set("instructions", JsonValue::MakeString(kb::script::ScriptAgentProjectFiles::AgentRules()));
            if (!isNotification) {
                WriteResult(io.out, *id, std::move(result));
            }
            continue;
        }
        if (methodName == "ping") {
            if (!isNotification) {
                WriteResult(io.out, *id, JsonValue::MakeObject());
            }
            continue;
        }
        if (methodName == "tools/list") {
            JsonValue result = JsonValue::MakeObject();
            tDiscovery = true;
            result.Set("tools", MakeToolList());
            tDiscovery = false;
            if (!isNotification) {
                WriteResult(io.out, *id, std::move(result));
            }
            continue;
        }
        if (methodName == "tools/call") {
            const JsonValue* params = request.Find("params");
            const JsonValue* name = params != nullptr ? params->Find("name") : nullptr;
            if (name == nullptr) {
                if (!isNotification) {
                    WriteError(io.out, *id, -32602, "tools/call requires params.name");
                }
                continue;
            }
            const JsonValue* toolArguments = params->Find("arguments");
            ToolCallOutcome outcome = DispatchTool(name->AsString(), toolArguments, projectRoot);
            NoteRepeatedFailure(failures, name->AsString() + "\n" + (toolArguments != nullptr ? toolArguments->Dump() : std::string{}), outcome);

            JsonValue textContent = JsonValue::MakeObject();
            textContent.Set("type", JsonValue::MakeString("text"));
            textContent.Set("text", JsonValue::MakeString(outcome.text));
            JsonValue content = JsonValue::MakeArray();
            // A capture is seen first, then read: the picture, then what it holds as text.
            for (const std::filesystem::path& picture : outcome.pictures) {
                CaptureForModel shown;
                std::string pictureError;
                if (!gImages || !DescribeCapture(picture, kPictureWidth, shown, pictureError)) continue;
                JsonValue image = JsonValue::MakeObject();
                image.Set("type", JsonValue::MakeString("image"));
                image.Set("data", JsonValue::MakeString(Base64Encode(shown.png)));
                image.Set("mimeType", JsonValue::MakeString("image/png"));
                content.Append(std::move(image));
            }
            content.Append(std::move(textContent));
            JsonValue result = JsonValue::MakeObject();
            result.Set("content", std::move(content));
            result.Set("isError", JsonValue::MakeBool(outcome.isError));
            if (!isNotification) {
                WriteResult(io.out, *id, std::move(result));
            }
            continue;
        }
        if (methodName.rfind("notifications/", 0U) == 0U) {
            continue;
        }

        if (!isNotification) {
            WriteError(io.out, *id, -32601, "method not found: " + methodName);
        }
    }

    static_cast<void>(io.err);
    return 0;
}

} // namespace kb::cli
