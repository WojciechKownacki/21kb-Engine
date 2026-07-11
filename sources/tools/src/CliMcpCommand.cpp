#include "CliCommands.hpp"
#include "MiniJson.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace kb::cli {

namespace {

constexpr std::string_view kProtocolVersion = "2024-11-05";
constexpr std::string_view kServerVersion = "0.1.0";

struct ToolProperty {
    std::string_view name;
    std::string_view type;
    std::string_view description;
    bool required = false;
};

[[nodiscard]] JsonValue MakeToolDescriptor(
    std::string_view name,
    std::string_view description,
    std::initializer_list<ToolProperty> properties) {
    JsonValue schema = JsonValue::MakeObject();
    schema.Set("type", JsonValue::MakeString("object"));
    JsonValue schemaProperties = JsonValue::MakeObject();
    JsonValue required = JsonValue::MakeArray();
    for (const ToolProperty& property : properties) {
        JsonValue descriptor = JsonValue::MakeObject();
        descriptor.Set("type", JsonValue::MakeString(std::string{ property.type }));
        descriptor.Set("description", JsonValue::MakeString(std::string{ property.description }));
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

[[nodiscard]] JsonValue MakeToolList() {
    JsonValue tools = JsonValue::MakeArray();
    tools.Append(MakeToolDescriptor(
        "api_reference",
        "Returns the 21kb Engine script API reference (functions, components, lifecycle events).",
        {
            ToolProperty{ "format", "string", "Output format: 'markdown' (default) or 'json'.", false },
        }));
    tools.Append(MakeToolDescriptor(
        "validate_script",
        "Validates a Lua behaviour script (syntax and sandbox load) and reports errors with line numbers.",
        {
            ToolProperty{ "path", "string", "Path to the .lua file, relative to the project root.", true },
        }));
    tools.Append(MakeToolDescriptor(
        "scene_list",
        "Lists the nodes of a .21kbscene file with their components and attached behaviours.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true },
        }));
    tools.Append(MakeToolDescriptor(
        "scene_attach",
        "Attaches a behaviour script asset to a named scene node and saves the scene.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true },
            ToolProperty{ "node", "string", "Name of the scene node to attach the behaviour to.", true },
            ToolProperty{ "script", "string", "Script asset path: /Game/... virtual path or a path under Assets/.", true },
            ToolProperty{ "tickGroup", "string", "Optional tick group: Input, Gameplay, Physics, Animation, Camera, Presentation.", false },
            ToolProperty{ "executionOrder", "number", "Optional execution order within the tick group.", false },
        }));
    tools.Append(MakeToolDescriptor(
        "run_scene",
        "Runs a scene headless for N frames and reports script logs, events, and errors.",
        {
            ToolProperty{ "scene", "string", "Scene path: physical (relative to project root) or virtual (/Game/...).", true },
            ToolProperty{ "frames", "number", "Frame count (default 60).", false },
            ToolProperty{ "dt", "number", "Delta seconds per frame (default 1/60).", false },
        }));
    return tools;
}

void WriteMessage(std::ostream& out, const JsonValue& message) {
    out << message.Dump() << '\n';
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

[[nodiscard]] ToolCallOutcome DispatchTool(
    std::string_view toolName,
    const JsonValue* toolArguments,
    const std::string& projectRoot) {
    std::vector<std::string> commandArguments;
    if (!projectRoot.empty()) {
        commandArguments.push_back("--project");
        commandArguments.push_back(projectRoot);
    }

    const auto pushOption = [&commandArguments](std::string_view option, std::string value) {
        if (!value.empty()) {
            commandArguments.emplace_back(option);
            commandArguments.push_back(std::move(value));
        }
    };

    int (*command)(const ArgumentList&, CommandIo) = nullptr;
    if (toolName == "api_reference") {
        std::string format = GetStringArgument(toolArguments, "format");
        if (format.empty()) {
            format = "markdown";
        }
        pushOption("--print", std::move(format));
        command = &RunApiCommand;
    } else if (toolName == "validate_script") {
        commandArguments.push_back(GetStringArgument(toolArguments, "path"));
        command = &RunValidateCommand;
    } else if (toolName == "scene_list") {
        pushOption("--scene", GetStringArgument(toolArguments, "scene"));
        command = &RunSceneListCommand;
    } else if (toolName == "scene_attach") {
        pushOption("--scene", GetStringArgument(toolArguments, "scene"));
        pushOption("--node", GetStringArgument(toolArguments, "node"));
        pushOption("--script", GetStringArgument(toolArguments, "script"));
        pushOption("--tick-group", GetStringArgument(toolArguments, "tickGroup"));
        if (toolArguments != nullptr) {
            if (const JsonValue* order = toolArguments->Find("executionOrder"); order != nullptr && !order->IsNull()) {
                pushOption("--execution-order", std::to_string(static_cast<int>(order->AsNumber())));
            }
        }
        command = &RunSceneAttachCommand;
    } else if (toolName == "run_scene") {
        pushOption("--scene", GetStringArgument(toolArguments, "scene"));
        if (toolArguments != nullptr) {
            if (const JsonValue* frames = toolArguments->Find("frames"); frames != nullptr && !frames->IsNull()) {
                pushOption("--frames", std::to_string(static_cast<int>(frames->AsNumber())));
            }
            if (const JsonValue* dt = toolArguments->Find("dt"); dt != nullptr && !dt->IsNull()) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%.9g", dt->AsNumber());
                pushOption("--dt", buffer);
            }
        }
        command = &RunRunCommand;
    } else {
        return ToolCallOutcome{ .isError = true, .text = "unknown tool: " + std::string{ toolName } };
    }

    std::ostringstream captured;
    const ArgumentList parsedArguments{ commandArguments, std::array<std::string_view, 1>{ "--disabled" } };
    const int exitCode = command(parsedArguments, CommandIo{ .out = captured, .err = captured });
    return ToolCallOutcome{ .isError = exitCode != 0, .text = captured.str() };
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
