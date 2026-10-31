#include "CliCommands.hpp"
#include "CliProgram.hpp"
#include "engine/core/JsonValue.hpp"

#include <array>
#include <cstdio>
#include <cmath>
#include <exception>
#include <limits>
#include <initializer_list>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::cli {
using kb::core::JsonValue;

namespace {

constexpr std::string_view kProtocolVersion = "2024-11-05";
constexpr std::string_view kServerVersion = "0.1.0";
constexpr std::string_view kEntityActions[]{"add", "remove", "rename", "attach", "detach"};
constexpr std::string_view kComponentActions[]{"add", "set", "remove"};
constexpr std::string_view kSceneTemplates[]{"Basic", "Empty"};
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
        descriptor.Set("x-cli-option", JsonValue::MakeString(std::string{property.cliOption}));
        if (property.choices.size() != 0U) {
            auto choices = JsonValue::MakeArray();
            for (const auto choice : property.choices) choices.Append(JsonValue::MakeString(std::string{choice}));
            descriptor.Set("enum", std::move(choices));
        }
        if (property.name == "topic") descriptor.Set("default", JsonValue::MakeString("index"));
        if (property.name == "inputReplay" || property.name == "inputRecord")
            descriptor.Set("x-project-file", JsonValue::MakeBool(true));
        if (property.type == "array") {
            auto items = JsonValue::MakeObject();
            items.Set("type", JsonValue::MakeString("string"));
            items.Set("minLength", JsonValue::MakeNumber(1));
            descriptor.Set("items", std::move(items));
        }
        if (property.type == "string") descriptor.Set("minLength", JsonValue::MakeNumber(property.name == "value" ? 0 : 1));
        if (property.type == "integer") {
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
        "Returns one page of the 21kb Engine script API: without arguments the index of function groups and components; "
        "with topic a group (Physics), one function (Physics.Raycast) or a component (Rigidbody); with search the topics matching the text. "
        "Ask for the one function or component you are about to use.",
        {
            ToolProperty{ "topic", "string", "A group, function or component name, or 'index'.", false, "--topic" },
            ToolProperty{ "search", "string", "Words to find in function and component names and descriptions.", false, "--search" },
        });
    if (command.name == "guide") return MakeToolDescriptor(command.mcpTool,
        "Returns a short how-to for one kind of task, with the kb_cli commands that do it: objects-and-materials, physics, scripts, "
        "characters-and-animation, combat-and-ai, maps-2d, verify. Without a topic, the list of guides.",
        {
            ToolProperty{ "topic", "string", "The guide's name, or 'index'.", false, "--topic" },
        });
    if (command.name == "import") return MakeToolDescriptor(command.mcpTool,
        "Imports an external source asset into a project's runtime asset format and registers it.",
        {
            ToolProperty{ "source", "string", "Physical source file path, or a path relative to the project root.", true, "" },
            ToolProperty{ "destination", "string", "Destination virtual folder, for example /Game/Fonts.", true, "--destination" },
        });
    if (command.name == "validate") return MakeToolDescriptor(command.mcpTool,
        "Validates a Lua behaviour script (syntax and sandbox load) and reports errors with line numbers.",
        {
            ToolProperty{ "path", "string", "Path to the .lua file, relative to the project root.", true, "" },
        });
    if (command.name == "scene-list") return MakeToolDescriptor(command.mcpTool,
        "Lists the nodes of a .21kbscene file with their components and attached behaviours.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true, "--scene" },
        });
    if (command.name == "scene-attach") return MakeToolDescriptor(command.mcpTool,
        "Attaches a behaviour script asset to a named scene node and saves the scene.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true, "--scene" },
            ToolProperty{ "node", "string", "Name of the scene node to attach the behaviour to.", true, "--node" },
            ToolProperty{ "script", "string", "Script asset path: /Game/... virtual path or a path under Assets/.", true, "--script" },
            ToolProperty{ "tickGroup", "string", "Optional tick group: Input, Gameplay, Physics, Animation, Camera, Presentation.", false, "--tick-group" },
            ToolProperty{ "executionOrder", "integer", "Optional execution order within the tick group.", false, "--execution-order" },
        });
    if (command.name == "run") return MakeToolDescriptor(command.mcpTool,
        "Runs a scene headless for N frames and reports script logs, events, and errors.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true, "--scene" },
            ToolProperty{ "frames", "integer", "Frame count (default 60).", false, "--frames" },
            ToolProperty{ "dt", "number", "Delta seconds per frame (default 1/60).", false, "--dt" },
            ToolProperty{ "report", "string", "Use particles or input for a versioned execution report.", false, "--report" },
            ToolProperty{ "inputReplay", "string", "Recording file to replay with its per-tick durations.", false, "--input-replay" },
            ToolProperty{ "inputRecord", "string", "Recording file to write with input consumed by completed ticks.", false, "--input-record" },
            ToolProperty{ "viewport", "string", "Viewport WIDTHxHEIGHT in pixels; overrides recorded viewport sizes.", false, "--viewport" },
            ToolProperty{ "safeArea", "string", "Nonnegative LEFT,TOP,RIGHT,BOTTOM safe-area insets in viewport pixels.", false, "--safe-area" },
        });
    if (command.name == "schema") return MakeToolDescriptor(command.mcpTool,
        "Lists component properties and authoring presets.", {
        });
    if (command.name == "entity") return MakeToolDescriptor(command.mcpTool,
        "Creates and edits entities through the same scene authoring commands as the CLI.", {
            ToolProperty{"action", "string", "Entity operation.", true, "", kEntityActions},
            ToolProperty{"name", "string", "Entity name or hierarchy path.", true, "--name"},
            ToolProperty{"scene", "string", "Scene path; defaults to the startup scene.", false, "--scene"},
            ToolProperty{"create", "string", "Optional built-in shape or player preset.", false, "--create"},
            ToolProperty{"parent", "string", "Optional parent entity.", false, "--parent"},
            ToolProperty{"to", "string", "Rename target or socket attachment owner.", false, "--to"},
            ToolProperty{"socket", "string", "Socket or bone name.", false, "--socket"},
            ToolProperty{"position", "string", "Position x,y,z.", false, "--position"},
            ToolProperty{"rotation", "string", "Rotation in degrees x,y,z.", false, "--rotation"},
            ToolProperty{"scale", "string", "Scale x,y,z.", false, "--scale"},
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
        });
    return JsonValue::MakeNull();
}

// Runs any kb_cli command for the bound project, so an agent reaches every operation the guides
// describe (materials, animation, maps, abilities) and not only the ones with a tool of their own.
constexpr std::string_view kCliToolName = "kb_cli";

[[nodiscard]] JsonValue MakeCliToolDescriptor() {
    return MakeToolDescriptor(kCliToolName,
        "Runs one kb_cli command for this project, as in the guides: args are the words after kb_cli, for example "
        "[\"material-new\", \"--name\", \"Red\", \"--set\", \"Base Color=#E03C31\"]. The project is filled in; a --project in args is replaced. "
        "A command with no options prints its usage.",
        {
            ToolProperty{ "args", "array", "The command and its options, one word per item.", true, "" },
        });
}

[[nodiscard]] JsonValue MakeToolList() {
    auto tools = JsonValue::MakeArray();
    for (const auto& command : CliCommandCatalog())
        if (command.available && !command.mcpTool.empty()) tools.Append(MakeCommandToolDescriptor(command));
    tools.Append(MakeCliToolDescriptor());
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
};

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
                if (value->GetKind() != JsonValue::Kind::Array) return key + " must be an array of strings";
                for (std::size_t element = 0U; element < value->Size(); ++element)
                    if (value->At(element)->GetKind() != JsonValue::Kind::String || value->At(element)->AsString().empty())
                        return key + " must contain nonempty strings";
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

[[nodiscard]] ToolCallOutcome DispatchTool(
    std::string_view toolName,
    const JsonValue* toolArguments,
    const std::string& projectRoot) {
    if (const auto error = ValidateToolArguments(toolName, toolArguments); !error.empty())
        return {.isError = true, .text = error};
    if (toolName == kCliToolName) {
        const JsonValue* args = toolArguments->Find("args");
        std::vector<std::string> words;
        for (std::size_t index = 0U; index < args->Size(); ++index) {
            const std::string& word = args->At(index)->AsString();
            if (word == "kb_cli" && words.empty()) continue;
            if (word == "--project") {
                ++index;
                continue;
            }
            words.push_back(word);
        }
        if (words.empty()) return {.isError = true, .text = "args must name a command"};
        const CliCommandDescriptor* target = FindCliCommand(words.front());
        if (target == nullptr) return {.isError = true, .text = "unknown kb_cli command: " + words.front()};
        if (target->kind == CliCommandKind::Mcp) return {.isError = true, .text = "the mcp command cannot run inside the MCP server"};
        // Commands about the project get it; the ones that make a project or describe kb_cli do not take it.
        const bool takesProject = !projectRoot.empty() && target->kind == CliCommandKind::Normal && words.front() != "new";
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
        return {.isError = exitCode != 0, .text = captured.str()};
    }
    const CliCommandDescriptor* command = nullptr;
    for (const auto& entry : CliCommandCatalog())
        if (entry.available && entry.mcpTool == toolName) { command = &entry; break; }
    if (command == nullptr) return {.isError = true, .text = "unknown tool: " + std::string{toolName}};
    if (projectRoot.empty() && command->name != "api" && command->name != "schema" && command->name != "guide")
        return {.isError = true, .text = "this tool requires a bound project"};
    std::vector<std::string> words{std::string{command->name}};
    if (!projectRoot.empty() && command->name != "guide") words.insert(words.end(), {"--project", projectRoot});
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
            JsonValue result = JsonValue::MakeObject();
            result.Set("protocolVersion", JsonValue::MakeString(std::string{ kProtocolVersion }));
            result.Set("capabilities", std::move(capabilities));
            result.Set("serverInfo", std::move(serverInfo));
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
            result.Set("tools", MakeToolList());
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
            const ToolCallOutcome outcome = DispatchTool(name->AsString(), toolArguments, projectRoot);

            JsonValue textContent = JsonValue::MakeObject();
            textContent.Set("type", JsonValue::MakeString("text"));
            textContent.Set("text", JsonValue::MakeString(outcome.text));
            JsonValue content = JsonValue::MakeArray();
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
