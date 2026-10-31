#include "engine/script/ScriptApiExport.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>
#include <vector>

namespace kb::script {

namespace {

[[nodiscard]] std::string_view LuaTypeName(ScriptValueType type) noexcept {
    switch (type) {
    case ScriptValueType::Bool:
        return "boolean";
    case ScriptValueType::Int:
        return "integer";
    case ScriptValueType::Float:
        return "number";
    case ScriptValueType::String:
        return "string";
    case ScriptValueType::Entity:
        return "integer";
    case ScriptValueType::Component:
        return "integer";
    case ScriptValueType::Int64:
        return "integer";
    case ScriptValueType::UInt32:
        return "integer";
    case ScriptValueType::Hash:
        return "integer";
    case ScriptValueType::Double:
        return "number";
    case ScriptValueType::Name:
        return "string";
    case ScriptValueType::Guid:
        return "string";
    case ScriptValueType::Void:
        break;
    }
    return "any";
}

[[nodiscard]] std::string PinList(const std::vector<ScriptApiPin>& pins) {
    if (pins.empty()) {
        return "—";
    }
    std::string text;
    for (const ScriptApiPin& pin : pins) {
        if (!text.empty()) {
            text += ", ";
        }
        text += pin.name;
        text += ": ";
        text += ToString(pin.type);
        if (!pin.required) {
            text += "?";
        }
    }
    return text;
}

[[nodiscard]] std::string FunctionGroup(std::string_view functionName) {
    const std::size_t dot = functionName.find('.');
    if (dot == std::string_view::npos) {
        return "Global";
    }
    return std::string{ functionName.substr(0, dot) };
}

void AppendJsonEscaped(std::string& out, std::string_view text) {
    for (const char character : text) {
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr std::string_view kHexDigits = "0123456789abcdef";
                out += "\\u00";
                out += kHexDigits[static_cast<std::size_t>(static_cast<unsigned char>(character) >> 4U)];
                out += kHexDigits[static_cast<std::size_t>(static_cast<unsigned char>(character) & 0x0FU)];
            } else {
                out += character;
            }
            break;
        }
    }
}

void AppendJsonString(std::string& out, std::string_view text) {
    out += '"';
    AppendJsonEscaped(out, text);
    out += '"';
}

void AppendJsonPins(std::string& out, const std::vector<ScriptApiPin>& pins) {
    out += '[';
    bool first = true;
    for (const ScriptApiPin& pin : pins) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += "{\"name\":";
        AppendJsonString(out, pin.name);
        out += ",\"type\":";
        AppendJsonString(out, ToString(pin.type));
        out += ",\"required\":";
        out += pin.required ? "true" : "false";
        out += '}';
    }
    out += ']';
}

[[nodiscard]] std::string StubResultClassName(const ScriptApiCatalogLuaBinding& binding) {
    std::string name = "Kb";
    name += binding.tableName;
    name += binding.luaName;
    name += "Result";
    return name;
}

[[nodiscard]] bool StubNeedsResultClass(const ScriptApiCatalogLuaBinding& binding) noexcept {
    return binding.returnKind == ScriptApiCatalogLuaReturnKind::OutputsTable
        || binding.returnKind == ScriptApiCatalogLuaReturnKind::GuardedTable;
}

void AppendStubReturn(
    std::string& out,
    const ScriptApiCatalogFunction& function,
    const ScriptApiCatalogLuaBinding& binding) {
    switch (binding.returnKind) {
    case ScriptApiCatalogLuaReturnKind::SingleOutput:
        for (const ScriptApiPin& pin : function.outputs) {
            if (pin.name == binding.returnPin) {
                out += "---@return ";
                out += LuaTypeName(pin.type);
                out += ' ';
                out += pin.name;
                out += '\n';
                return;
            }
        }
        return;
    case ScriptApiCatalogLuaReturnKind::OutputsTable:
        out += "---@return ";
        out += StubResultClassName(binding);
        out += '\n';
        return;
    case ScriptApiCatalogLuaReturnKind::GuardedTable:
        out += "---@return ";
        out += StubResultClassName(binding);
        out += "|nil\n";
        return;
    case ScriptApiCatalogLuaReturnKind::Default:
        break;
    }
    if (function.outputs.size() == 1U) {
        out += "---@return ";
        out += LuaTypeName(function.outputs.front().type);
        out += ' ';
        out += function.outputs.front().name;
        out += '\n';
    } else if (function.outputs.size() > 1U) {
        out += "---@return table<string, any>\n";
    }
}

void AppendStubFunction(
    std::string& out,
    const ScriptApiCatalog& catalog,
    const ScriptApiCatalogLuaBinding& binding) {
    const ScriptApiCatalogFunction* function = catalog.FindFunction(binding.functionName);
    const std::string qualifiedName = binding.tableName.empty()
        ? binding.luaName
        : binding.tableName + "." + binding.luaName;

    if (function != nullptr) {
        out += "---";
        out += function->description;
        out += '\n';
    } else {
        out += "---Forwards to the engine function `";
        out += binding.functionName;
        out += "`.\n";
    }
    if (binding.returnKind == ScriptApiCatalogLuaReturnKind::GuardedTable) {
        out += "---Returns nil when the engine reports `";
        out += binding.returnPin;
        out += "` as false.\n";
    }

    std::string parameterList;
    if (function == nullptr) {
        out += "---@param ... any\n";
        parameterList = "...";
    } else {
        for (const ScriptApiPin& pin : function->inputs) {
            out += "---@param ";
            out += pin.name;
            if (!pin.required) {
                out += '?';
            }
            out += ' ';
            out += LuaTypeName(pin.type);
            out += '\n';
            if (!parameterList.empty()) {
                parameterList += ", ";
            }
            parameterList += pin.name;
        }
        AppendStubReturn(out, *function, binding);
    }

    out += "function ";
    out += qualifiedName;
    out += '(';
    out += parameterList;
    out += ") end\n\n";
}

void AppendStubResultClasses(std::string& out, const ScriptApiCatalog& catalog) {
    for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        const ScriptApiCatalogFunction* function = catalog.FindFunction(binding.functionName);
        if (function == nullptr || !StubNeedsResultClass(binding)) {
            continue;
        }
        out += "---@class ";
        out += StubResultClassName(binding);
        out += '\n';
        for (const ScriptApiPin& pin : function->outputs) {
            if (binding.returnKind == ScriptApiCatalogLuaReturnKind::GuardedTable && pin.name == binding.returnPin) {
                continue;
            }
            out += "---@field ";
            out += pin.name;
            out += ' ';
            out += LuaTypeName(pin.type);
            out += '\n';
        }
        out += '\n';
    }
}

[[nodiscard]] std::string LuaBindingReturnSummary(
    const ScriptApiCatalog& catalog,
    const ScriptApiCatalogLuaBinding& binding) {
    const ScriptApiCatalogFunction* function = catalog.FindFunction(binding.functionName);
    if (function == nullptr) {
        return "—";
    }
    switch (binding.returnKind) {
    case ScriptApiCatalogLuaReturnKind::SingleOutput:
        for (const ScriptApiPin& pin : function->outputs) {
            if (pin.name == binding.returnPin) {
                return binding.returnPin + ": " + ToString(pin.type);
            }
        }
        return "—";
    case ScriptApiCatalogLuaReturnKind::OutputsTable:
        return "table { " + PinList(function->outputs) + " }";
    case ScriptApiCatalogLuaReturnKind::GuardedTable: {
        std::vector<ScriptApiPin> visible;
        for (const ScriptApiPin& pin : function->outputs) {
            if (pin.name != binding.returnPin) {
                visible.push_back(pin);
            }
        }
        return "table { " + PinList(visible) + " } or nil when " + binding.returnPin + " is false";
    }
    case ScriptApiCatalogLuaReturnKind::Default:
        break;
    }
    if (function->outputs.empty()) {
        return "—";
    }
    if (function->outputs.size() == 1U) {
        return function->outputs.front().name + ": " + ToString(function->outputs.front().type);
    }
    return "table { " + PinList(function->outputs) + " }";
}

[[nodiscard]] std::string CppExampleValue(ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Bool:
        return "false";
    case ScriptValueType::Int:
        return "int{0}";
    case ScriptValueType::Float:
        return "0.0F";
    case ScriptValueType::String:
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return "std::string{}";
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Hash:
        return "std::uint64_t{0}";
    case ScriptValueType::UInt32:
        return "std::uint32_t{0}";
    case ScriptValueType::Int64:
        return "std::int64_t{0}";
    case ScriptValueType::Double:
        return "0.0";
    case ScriptValueType::Void:
        break;
    }
    return "{}";
}

[[nodiscard]] std::string LuaExampleValue(ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Bool:
        return "false";
    case ScriptValueType::Int:
    case ScriptValueType::Float:
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Int64:
    case ScriptValueType::UInt32:
    case ScriptValueType::Double:
    case ScriptValueType::Hash:
        return "0";
    case ScriptValueType::String:
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return "\"\"";
    case ScriptValueType::Void:
        break;
    }
    return "nil";
}

void AppendFunctionExamples(std::string& out, const ScriptApiCatalogFunction& function) {
    out += "<a id=\"";
    out += ScriptApiDocumentationAnchor(function.name);
    out += "\"></a>\n\n";
    out += "#### `";
    out += function.name;
    out += "` examples\n\n**C++**\n\n```cpp\ncontext.CallFunction(\"";
    out += function.name;
    out += "\", {\n";
    for (const ScriptApiPin& pin : function.inputs) {
        out += "    { \"";
        out += pin.name;
        out += "\", kb::script::ScriptValue{ ";
        out += CppExampleValue(pin.type);
        if (pin.type == ScriptValueType::Entity || pin.type == ScriptValueType::Component || pin.type == ScriptValueType::Hash ||
            pin.type == ScriptValueType::Name || pin.type == ScriptValueType::Guid) {
            out += ", kb::script::ScriptValueType::";
            out += ToString(pin.type);
        }
        out += " } },\n";
    }
    out += "});\n```\n\n**Lua**\n\n```lua\nlocal result = CallFunction(\"";
    out += function.name;
    out += "\", {\n";
    for (const ScriptApiPin& pin : function.inputs) {
        out += "    ";
        out += pin.name;
        out += " = ";
        out += LuaExampleValue(pin.type);
        out += ",\n";
    }
    out += "})\n```\n\n**Visual Graph**\n\nUse the `Function.";
    out += function.name;
    out += "` CallNative node with inputs `";
    out += PinList(function.inputs);
    out += "` and outputs `";
    out += PinList(function.outputs);
    out += "`.\n\n";
}

} // namespace

std::string ScriptApiExport::ToMarkdown(const ScriptApiCatalog& catalog) {
    std::string out;
    out.reserve(16U * 1024U);

    out += "# 21kb Engine — Script API Reference\n\n";
    out += "> Generated with `kb_cli api`. Do not edit by hand; regenerate after engine updates.\n\n";

    out += "## Behaviour model\n\n";
    out += "Entities carry a `Behaviour` component that references a script asset. Backends: `Lua` (`.lua` files), ";
    out += "`VisualGraph` (`.kbgraph` files), `Native` (C++ plugin descriptors, `.native`/`.kbnative`). ";
    out += "Behaviours run in tick groups (Input, Gameplay, Physics, Animation, Camera, Presentation) ordered by `executionOrder`.\n\n";

    out += "## Lifecycle events\n\n";
    out += "A Lua behaviour implements lifecycle hooks by defining global functions named exactly after the event. ";
    out += "Lifecycle hooks receive `(self, dt)`. Custom events are handled by defining a global function named after ";
    out += "the event, receiving `(self, event)`.\n\n";
    out += "| Event | Lua signature |\n|---|---|\n";
    for (const std::string& event : catalog.lifecycleEvents) {
        out += "| ";
        out += event;
        out += " | `function ";
        out += event;
        out += "(self, dt)` |\n";
    }
    out += '\n';

    out += "## Lua sandbox globals\n\n";
    out += "Scripts run in a sandbox (no `io`, `os`, `require`, `load`). Available besides the tables below: ";
    out += "`Emit(name, args?)`, `EmitTo(entityId, name, args?)`, `SetShared(key, value)`, `GetShared(key)`, ";
    out += "`HasShared(key)`, `RemoveShared(key)`, `Import(moduleName)`, `CallFunction(name, argsTable?)`, `Log(value)`, ";
    out += "plus safe Lua standard libraries (`math`, `string`, `table`, `coroutine`, `utf8`).\n\n";
    out += "`self` fields: `entity`, `asset`, `backend`, `variables`. `self` methods: `self:HasComponent(name)`, ";
    out += "`self:GetProperty(component, property)`, `self:SetProperty(component, property, value)`, ";
    out += "`self:GetVariable(name)`, `self:SetVariable(name, value)`.\n\n";

    out += "## Engine functions\n\n";
    out += "Callable from Lua via the convenience tables below or `CallFunction(\"<name>\", { ... })`, and from ";
    out += "Visual Graphs as `Function.<name>` CallNative nodes. `CallFunction` returns `nil` for zero outputs, the ";
    out += "value for one output, and a table of named outputs otherwise.\n\n";

    std::map<std::string, std::vector<const ScriptApiCatalogFunction*>> groups;
    for (const ScriptApiCatalogFunction& function : catalog.functions) {
        groups[FunctionGroup(function.name)].push_back(&function);
    }
    for (const auto& [group, functions] : groups) {
        out += "### ";
        out += group;
        out += "\n\n| Function | Description | Inputs | Outputs |\n|---|---|---|---|\n";
        for (const ScriptApiCatalogFunction* function : functions) {
            out += "| `";
            out += function->name;
            out += "` | ";
            out += function->description;
            out += " | ";
            out += PinList(function->inputs);
            out += " | ";
            out += PinList(function->outputs);
            out += " |\n";
        }
        out += '\n';
        for (const ScriptApiCatalogFunction* function : functions) {
            AppendFunctionExamples(out, *function);
        }
    }

    out += "## Lua convenience bindings\n\n";
    out += "| Lua call | Forwards to | Returns |\n|---|---|---|\n";
    for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        out += "| `";
        if (!binding.tableName.empty()) {
            out += binding.tableName;
            out += '.';
        }
        out += binding.luaName;
        out += "(...)` | `";
        out += binding.functionName;
        out += "` | ";
        out += LuaBindingReturnSummary(catalog, binding);
        out += " |\n";
    }
    out += '\n';

    out += "## Components\n\n";
    out += "Usable with `self:HasComponent`, `self:GetProperty`, `self:SetProperty`.\n\n";
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        out += "### ";
        out += component.name;
        out += "\n\n| Property | Type | Access |\n|---|---|---|\n";
        for (const ScriptApiCatalogProperty& property : component.properties) {
            out += "| `";
            out += property.name;
            out += "` | ";
            out += ToString(property.type);
            out += " | ";
            out += property.writable ? "read/write" : "read-only";
            out += " |\n";
        }
        out += '\n';
    }

    if (!catalog.projectEntries.empty()) {
        out += "## Project-declared API\n\n";
        out += "Collected from the project's script assets.\n\n";
        out += "| Kind | Name | Owner | Contract |\n|---|---|---|---|\n";
        for (const ScriptApiNameEntry& entry : catalog.projectEntries) {
            out += "| ";
            out += ToString(entry.kind);
            out += " | `";
            out += entry.name;
            out += "` | ";
            out += entry.owner.empty() ? "—" : entry.owner;
            out += " | ";
            if (entry.kind == ScriptApiNameKind::Function || entry.kind == ScriptApiNameKind::Event) {
                out += "in: ";
                out += PinList(entry.inputs);
                if (entry.kind == ScriptApiNameKind::Function) {
                    out += "; out: ";
                    out += PinList(entry.outputs);
                }
            } else {
                out += ToString(entry.valueType);
            }
            out += " |\n";
        }
        out += '\n';
    }

    return out;
}

namespace {

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{ text };
    std::ranges::transform(lower, lower.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

// A name as a file name: letters, digits, spaces and - _ . kept, anything else replaced.
[[nodiscard]] std::string PageFileName(std::string_view name) {
    std::string file;
    for (const char character : name) {
        const unsigned char code = static_cast<unsigned char>(character);
        file += std::isalnum(code) != 0 || character == ' ' || character == '-' || character == '_' || character == '.' ? character : '_';
    }
    return file.empty() ? std::string{ "_" } : file;
}

[[nodiscard]] std::string FunctionShortName(std::string_view functionName) {
    const std::size_t dot = functionName.find('.');
    return std::string{ dot == std::string_view::npos ? functionName : functionName.substr(dot + 1U) };
}

[[nodiscard]] std::string FunctionPagePath(std::string_view functionName) {
    return "functions/" + PageFileName(FunctionGroup(functionName)) + "/" + PageFileName(FunctionShortName(functionName)) + ".md";
}

[[nodiscard]] std::string GroupPagePath(std::string_view group) {
    return "functions/" + PageFileName(group) + ".md";
}

[[nodiscard]] std::string ComponentPagePath(std::string_view component) {
    return "components/" + PageFileName(component) + ".md";
}

[[nodiscard]] const ScriptApiCatalogLuaBinding* LuaBindingOf(const ScriptApiCatalog& catalog, std::string_view functionName) {
    for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        if (binding.functionName == functionName) {
            return &binding;
        }
    }
    return nullptr;
}

// How a script calls the function: its Lua convenience call when there is one, otherwise
// CallFunction.
[[nodiscard]] std::string LuaCall(const ScriptApiCatalog& catalog, const ScriptApiCatalogFunction& function) {
    std::string arguments;
    for (const ScriptApiPin& pin : function.inputs) {
        if (!arguments.empty()) {
            arguments += ", ";
        }
        arguments += pin.name;
    }
    if (const ScriptApiCatalogLuaBinding* binding = LuaBindingOf(catalog, function.name); binding != nullptr) {
        return (binding->tableName.empty() ? std::string{} : binding->tableName + ".") + binding->luaName + "(" + arguments + ")";
    }
    return "CallFunction(\"" + function.name + "\", { " + arguments + " })";
}

[[nodiscard]] std::map<std::string, std::vector<const ScriptApiCatalogFunction*>> FunctionGroups(const ScriptApiCatalog& catalog) {
    std::map<std::string, std::vector<const ScriptApiCatalogFunction*>> groups;
    for (const ScriptApiCatalogFunction& function : catalog.functions) {
        groups[FunctionGroup(function.name)].push_back(&function);
    }
    return groups;
}

[[nodiscard]] std::string IndexPage(const ScriptApiCatalog& catalog) {
    std::string out;
    out += "# 21kb Engine script API\n\n";
    out += "Read only the page your task needs. The kb-engine `api_reference` tool returns one page by `topic` ";
    out += "(a group such as `Physics`, a function such as `Physics.Raycast`, a component such as `Rigidbody`) ";
    out += "or finds topics by `search` text; the same pages are files next to this one.\n\n";
    out += "## How scripts work\n\n";
    out += "A `Behaviour` component runs a script asset on its entity: Lua (`.lua`), Visual Graph (`.kbgraph`) or native C++. ";
    out += "A Lua script defines global functions named after lifecycle events, each receiving `(self, dt)`: ";
    for (std::size_t index = 0; index < catalog.lifecycleEvents.size(); ++index) {
        out += (index == 0U ? "`" : ", `") + catalog.lifecycleEvents[index] + "`";
    }
    out += ". A custom event `Name` calls `function Name(self, event)`.\n\n";
    out += "`self` has `entity`, `asset`, `backend`, `variables` and the methods `HasComponent(name)`, ";
    out += "`GetProperty(component, property)`, `SetProperty(component, property, value)`, `GetVariable(name)`, `SetVariable(name, value)`. ";
    out += "The sandbox has no `io`, `os`, `require` or `load`; it adds `Emit`, `EmitTo`, `SetShared`, `GetShared`, `Import`, ";
    out += "`CallFunction(name, args)` and `Log`. Visual Graphs call the same functions as `Function.<name>` nodes.\n\n";
    out += "## Function groups\n\n";
    for (const auto& [group, functions] : FunctionGroups(catalog)) {
        out += "- [" + group + "](" + GroupPagePath(group) + ") - " + std::to_string(functions.size()) + " functions: ";
        for (std::size_t index = 0; index < functions.size() && index < 6U; ++index) {
            out += (index == 0U ? "" : ", ") + FunctionShortName(functions[index]->name);
        }
        out += functions.size() > 6U ? ", ...\n" : "\n";
    }
    out += "\n## Components\n\n";
    out += "Read and change their properties with `self:GetProperty` and `self:SetProperty`, or the `component_edit` tool.\n\n";
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        out += "- [" + component.name + "](" + ComponentPagePath(component.name) + ") - " + std::to_string(component.properties.size()) + " properties\n";
    }
    return out;
}

[[nodiscard]] std::string GroupPage(const ScriptApiCatalog& catalog, const std::string& group, const std::vector<const ScriptApiCatalogFunction*>& functions) {
    std::string out = "# " + group + " functions\n\n| Function | Lua call | What it does |\n|---|---|---|\n";
    for (const ScriptApiCatalogFunction* function : functions) {
        out += "| [`" + function->name + "`](" + PageFileName(group) + "/" + PageFileName(FunctionShortName(function->name)) + ".md) | `" +
            LuaCall(catalog, *function) + "` | " + function->description + " |\n";
    }
    out += "\nEach function has its own page with its inputs, outputs and an example.\n";
    return out;
}

[[nodiscard]] std::string FunctionPage(const ScriptApiCatalog& catalog, const ScriptApiCatalogFunction& function) {
    std::string out = "# " + function.name + "\n\n" + function.description + "\n\n";
    if (!function.inputs.empty()) {
        out += "| Input | Type | Required |\n|---|---|---|\n";
        for (const ScriptApiPin& pin : function.inputs) {
            out += "| `" + pin.name + "` | " + std::string{ ToString(pin.type) } + " | " + (pin.required ? "yes" : "no") + " |\n";
        }
        out += '\n';
    }
    if (!function.outputs.empty()) {
        out += "| Output | Type |\n|---|---|\n";
        for (const ScriptApiPin& pin : function.outputs) {
            out += "| `" + pin.name + "` | " + std::string{ ToString(pin.type) } + " |\n";
        }
        out += '\n';
    }
    const ScriptApiCatalogLuaBinding* binding = LuaBindingOf(catalog, function.name);
    out += "Lua: `" + LuaCall(catalog, function) + "`";
    if (binding != nullptr) {
        out += " returns " + LuaBindingReturnSummary(catalog, *binding);
    }
    out += ".\n\n```lua\n";
    out += function.outputs.empty() ? "" : "local result = ";
    if (binding != nullptr) {
        out += (binding->tableName.empty() ? std::string{} : binding->tableName + ".") + binding->luaName + "(";
        for (std::size_t index = 0; index < function.inputs.size(); ++index) {
            out += (index == 0U ? "" : ", ") + LuaExampleValue(function.inputs[index].type);
        }
        out += ")\n";
    } else {
        out += "CallFunction(\"" + function.name + "\", {";
        for (std::size_t index = 0; index < function.inputs.size(); ++index) {
            out += (index == 0U ? " " : ", ") + function.inputs[index].name + " = " + LuaExampleValue(function.inputs[index].type);
        }
        out += function.inputs.empty() ? "})\n" : " })\n";
    }
    out += "```\n\nVisual Graph: the `Function." + function.name + "` node.\n";
    return out;
}

[[nodiscard]] std::string ComponentPage(const ScriptApiCatalogComponent& component) {
    std::string out = "# " + component.name + " component\n\n";
    out += "Script access: `self:GetProperty(\"" + component.name + "\", property)` and `self:SetProperty(\"" + component.name +
        "\", property, value)`; tools: `component_edit` with `type: \"" + component.name + "\"` and `set: [\"property=value\"]`.\n\n";
    out += "| Property | Type | Access |\n|---|---|---|\n";
    for (const ScriptApiCatalogProperty& property : component.properties) {
        out += "| `" + property.name + "` | " + std::string{ ToString(property.type) } + " | " + (property.writable ? "read/write" : "read-only") + " |\n";
    }
    return out;
}

} // namespace

std::vector<ScriptApiReferencePage> ScriptApiExport::ToReferencePages(const ScriptApiCatalog& catalog) {
    std::vector<ScriptApiReferencePage> pages;
    pages.push_back({ "README.md", IndexPage(catalog) });
    for (const auto& [group, functions] : FunctionGroups(catalog)) {
        pages.push_back({ GroupPagePath(group), GroupPage(catalog, group, functions) });
        for (const ScriptApiCatalogFunction* function : functions) {
            pages.push_back({ FunctionPagePath(function->name), FunctionPage(catalog, *function) });
        }
    }
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        pages.push_back({ ComponentPagePath(component.name), ComponentPage(component) });
    }
    return pages;
}

std::optional<std::string> ScriptApiExport::ToReferenceTopic(const ScriptApiCatalog& catalog, std::string_view topic) {
    std::string wanted = Lower(topic);
    while (!wanted.empty() && wanted.back() == ' ') wanted.pop_back();
    while (!wanted.empty() && wanted.front() == ' ') wanted.erase(wanted.begin());
    std::ranges::replace(wanted, '/', '.');
    if (wanted.empty() || wanted == "index" || wanted == "readme") {
        return IndexPage(catalog);
    }
    for (const ScriptApiCatalogFunction& function : catalog.functions) {
        if (Lower(function.name) == wanted) return FunctionPage(catalog, function);
    }
    const auto groups = FunctionGroups(catalog);
    for (const auto& [group, functions] : groups) {
        if (Lower(group) == wanted) return GroupPage(catalog, group, functions);
    }
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        if (Lower(component.name) == wanted) return ComponentPage(component);
    }
    // A function by its short name, when only one group has it.
    const ScriptApiCatalogFunction* only = nullptr;
    for (const ScriptApiCatalogFunction& function : catalog.functions) {
        if (Lower(FunctionShortName(function.name)) == wanted) {
            if (only != nullptr) return std::nullopt;
            only = &function;
        }
    }
    if (only != nullptr) return FunctionPage(catalog, *only);
    return std::nullopt;
}

std::string ScriptApiExport::SearchReference(const ScriptApiCatalog& catalog, std::string_view text, std::size_t limit) {
    std::vector<std::string> words;
    std::string word;
    for (const char character : Lower(text) + " ") {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '.' || character == '_') {
            word += character;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    const auto matches = [&words](const std::string& haystack) {
        return std::ranges::all_of(words, [&haystack](const std::string& part) { return haystack.find(part) != std::string::npos; });
    };
    std::string out;
    std::size_t found = 0U;
    for (const ScriptApiCatalogFunction& function : catalog.functions) {
        if (found < limit && !words.empty() && matches(Lower(function.name + " " + function.description))) {
            out += "- `" + function.name + "` - " + function.description + "\n";
            ++found;
        }
    }
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        std::string haystack = Lower(component.name);
        for (const ScriptApiCatalogProperty& property : component.properties) {
            haystack += " " + Lower(property.name);
        }
        if (found < limit && !words.empty() && matches(haystack)) {
            out += "- `" + component.name + "` - component with " + std::to_string(component.properties.size()) + " properties\n";
            ++found;
        }
    }
    if (found == 0U) {
        return "Nothing in the script API matches \"" + std::string{ text } + "\". Ask for the index (no topic) to see the groups and components.\n";
    }
    return "Ask for one of these as the topic:\n\n" + out;
}

std::string ScriptApiExport::ToLuaStubs(const ScriptApiCatalog& catalog) {
    std::string out;
    out.reserve(16U * 1024U);

    out += "---@meta\n";
    out += "-- 21kb Engine script sandbox definitions. Generated with `kb_cli api`; do not edit.\n";
    out += "-- Lifecycle hooks: define global functions named ";
    for (std::size_t index = 0; index < catalog.lifecycleEvents.size(); ++index) {
        if (index != 0U) {
            out += ", ";
        }
        out += catalog.lifecycleEvents[index];
    }
    out += " taking (self, dt).\n";
    out += "-- Custom event handlers: define a global function named after the event taking (self, event).\n\n";

    out += "---@alias KbComponentName\n";
    for (const ScriptApiCatalogComponent& component : catalog.components) {
        out += "---| '\"";
        out += component.name;
        out += "\"'\n";
    }
    out += '\n';

    out += "---@class KbEvent\n";
    out += "---@field name string\n";
    out += "---@field sender integer\n";
    out += "---@field target integer\n";
    out += "---@field senderAsset integer\n";
    out += "---@field args table<string, any>\n\n";

    out += "---@class KbSelf\n";
    out += "---@field entity integer\n";
    out += "---@field asset integer\n";
    out += "---@field backend string\n";
    out += "---@field variables table<string, any>\n";
    out += "local KbSelf = {}\n\n";
    out += "---@param component KbComponentName\n";
    out += "---@return boolean\n";
    out += "function KbSelf:HasComponent(component) end\n\n";
    out += "---@param component KbComponentName\n";
    out += "---@param property string\n";
    out += "---@return any value, string? error\n";
    out += "function KbSelf:GetProperty(component, property) end\n\n";
    out += "---@param component KbComponentName\n";
    out += "---@param property string\n";
    out += "---@param value any\n";
    out += "---@return boolean ok, string? error\n";
    out += "function KbSelf:SetProperty(component, property, value) end\n\n";
    out += "---@param name string\n";
    out += "---@return any value, string? error\n";
    out += "function KbSelf:GetVariable(name) end\n\n";
    out += "---@param name string\n";
    out += "---@param value any\n";
    out += "---@return boolean ok, string? error\n";
    out += "function KbSelf:SetVariable(name, value) end\n\n";

    out += "---Emits a script event to every behaviour in the scene.\n";
    out += "---@param name string\n";
    out += "---@param args? table<string, any>\n";
    out += "function Emit(name, args) end\n\n";
    out += "---Emits a script event to the behaviours of a single entity.\n";
    out += "---@param entity integer\n";
    out += "---@param name string\n";
    out += "---@param args? table<string, any>\n";
    out += "function EmitTo(entity, name, args) end\n\n";
    out += "---@param key string\n";
    out += "---@param value any\n";
    out += "---@return boolean\n";
    out += "function SetShared(key, value) end\n\n";
    out += "---@param key string\n";
    out += "---@return any\n";
    out += "function GetShared(key) end\n\n";
    out += "---@param key string\n";
    out += "---@return boolean\n";
    out += "function HasShared(key) end\n\n";
    out += "---@param key string\n";
    out += "---@return boolean\n";
    out += "function RemoveShared(key) end\n\n";
    out += "---Imports a registered script module (declared with `-- @import`).\n";
    out += "---@param moduleName string\n";
    out += "---@return any module, string? error\n";
    out += "function Import(moduleName) end\n\n";
    out += "---Calls any registered engine function by its qualified name.\n";
    out += "---Returns nil for zero outputs, the value for one output, a named table otherwise.\n";
    out += "---@param name string\n";
    out += "---@param args? table<string, any>\n";
    out += "---@return any\n";
    out += "function CallFunction(name, args) end\n\n";

    AppendStubResultClasses(out, catalog);

    std::vector<std::string> declaredTables;
    for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        if (!binding.tableName.empty()
            && std::find(declaredTables.begin(), declaredTables.end(), binding.tableName) == declaredTables.end()) {
            declaredTables.push_back(binding.tableName);
            out += binding.tableName;
            out += " = {}\n";
        }
    }
    out += '\n';

    for (const ScriptApiCatalogLuaBinding& binding : catalog.luaBindings) {
        AppendStubFunction(out, catalog, binding);
    }

    return out;
}

std::string ScriptApiExport::ToJson(const ScriptApiCatalog& catalog) {
    std::string out;
    out.reserve(16U * 1024U);

    out += "{\"generator\":\"kb_cli api\",\"lifecycleEvents\":[";
    for (std::size_t index = 0; index < catalog.lifecycleEvents.size(); ++index) {
        if (index != 0U) {
            out += ',';
        }
        AppendJsonString(out, catalog.lifecycleEvents[index]);
    }
    out += "],\"functions\":[";
    for (std::size_t index = 0; index < catalog.functions.size(); ++index) {
        const ScriptApiCatalogFunction& function = catalog.functions[index];
        if (index != 0U) {
            out += ',';
        }
        out += "{\"name\":";
        AppendJsonString(out, function.name);
        out += ",\"description\":";
        AppendJsonString(out, function.description);
        out += ",\"executionAffinity\":";
        AppendJsonString(out, kb::core::ToString(function.executionAffinity));
        out += ",\"inputs\":";
        AppendJsonPins(out, function.inputs);
        out += ",\"outputs\":";
        AppendJsonPins(out, function.outputs);
        out += '}';
    }
    out += "],\"luaBindings\":[";
    for (std::size_t index = 0; index < catalog.luaBindings.size(); ++index) {
        const ScriptApiCatalogLuaBinding& binding = catalog.luaBindings[index];
        if (index != 0U) {
            out += ',';
        }
        out += "{\"table\":";
        AppendJsonString(out, binding.tableName);
        out += ",\"name\":";
        AppendJsonString(out, binding.luaName);
        out += ",\"function\":";
        AppendJsonString(out, binding.functionName);
        out += ",\"returnKind\":";
        AppendJsonString(out, ToString(binding.returnKind));
        out += ",\"returnPin\":";
        AppendJsonString(out, binding.returnPin);
        out += '}';
    }
    out += "],\"sourceMap\":[";
    for (std::size_t index = 0; index < catalog.sourceMap.size(); ++index) {
        const ScriptApiCatalogSourceMapEntry& entry = catalog.sourceMap[index];
        if (index != 0U) {
            out += ',';
        }
        out += "{\"visualGraphNodeId\":";
        AppendJsonString(out, entry.visualGraphNodeId);
        out += ",\"visualGraphPin\":";
        AppendJsonString(out, entry.visualGraphPinName);
        out += ",\"visualGraphPinDirection\":";
        AppendJsonString(out, entry.visualGraphPinDirection);
        out += ",\"visualGraphNodeCategory\":";
        AppendJsonString(out, entry.visualGraphNodeCategory);
        out += ",\"function\":";
        AppendJsonString(out, entry.functionName);
        out += ",\"runtimeBindingSymbol\":";
        AppendJsonString(out, entry.runtimeBindingSymbol);
        out += ",\"documentationAnchor\":";
        AppendJsonString(out, entry.documentationAnchor);
        out += '}';
    }
    out += "],\"components\":[";
    for (std::size_t componentIndex = 0; componentIndex < catalog.components.size(); ++componentIndex) {
        const ScriptApiCatalogComponent& component = catalog.components[componentIndex];
        if (componentIndex != 0U) {
            out += ',';
        }
        out += "{\"name\":";
        AppendJsonString(out, component.name);
        out += ",\"properties\":[";
        for (std::size_t propertyIndex = 0; propertyIndex < component.properties.size(); ++propertyIndex) {
            const ScriptApiCatalogProperty& property = component.properties[propertyIndex];
            if (propertyIndex != 0U) {
                out += ',';
            }
            out += "{\"name\":";
            AppendJsonString(out, property.name);
            out += ",\"type\":";
            AppendJsonString(out, ToString(property.type));
            out += ",\"writable\":";
            out += property.writable ? "true" : "false";
            out += '}';
        }
        out += "]}";
    }
    out += "],\"projectEntries\":[";
    for (std::size_t index = 0; index < catalog.projectEntries.size(); ++index) {
        const ScriptApiNameEntry& entry = catalog.projectEntries[index];
        if (index != 0U) {
            out += ',';
        }
        out += "{\"kind\":";
        AppendJsonString(out, ToString(entry.kind));
        out += ",\"name\":";
        AppendJsonString(out, entry.name);
        out += ",\"owner\":";
        AppendJsonString(out, entry.owner);
        out += ",\"valueType\":";
        AppendJsonString(out, ToString(entry.valueType));
        out += ",\"inputs\":";
        AppendJsonPins(out, entry.inputs);
        out += ",\"outputs\":";
        AppendJsonPins(out, entry.outputs);
        out += '}';
    }
    out += "]}";

    return out;
}

} // namespace kb::script
