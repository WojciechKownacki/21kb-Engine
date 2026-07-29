#include "engine/library/EngineLibraryAuthoringHints.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace kb::library {
namespace {

[[nodiscard]] std::string Lowercase(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const unsigned char character : text) {
        lowered += static_cast<char>(std::tolower(character));
    }
    return lowered;
}

[[nodiscard]] bool Matches(std::string_view query, std::string_view searchableText) {
    const std::string loweredText = Lowercase(searchableText);
    const std::string loweredQuery = Lowercase(query);
    std::size_t position = 0U;
    while (position < loweredQuery.size()) {
        while (position < loweredQuery.size() && std::isspace(static_cast<unsigned char>(loweredQuery[position]))) {
            ++position;
        }
        const std::size_t tokenStart = position;
        while (position < loweredQuery.size() && !std::isspace(static_cast<unsigned char>(loweredQuery[position]))) {
            ++position;
        }
        if (tokenStart != position && loweredText.find(loweredQuery.substr(tokenStart, position - tokenStart)) == std::string::npos) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string PinNames(const std::vector<kb::script::ScriptApiPin>& pins) {
    std::string names;
    for (const kb::script::ScriptApiPin& pin : pins) {
        if (!names.empty()) {
            names += ", ";
        }
        names += pin.name;
    }
    return names;
}

[[nodiscard]] std::string LuaExample(const kb::script::ScriptApiCatalogFunction& function, const kb::script::ScriptApiCatalogLuaBinding& binding) {
    const std::string callable = binding.tableName.empty() ? binding.luaName : binding.tableName + "." + binding.luaName;
    std::string example;
    if (!function.outputs.empty()) {
        example = "local result = ";
    }
    example += callable + "(" + PinNames(function.inputs) + ")";
    return example;
}

void AppendJsonEscaped(std::string& output, std::string_view text) {
    output += '"';
    for (const char character : text) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr std::string_view kHexDigits = "0123456789abcdef";
                output += "\\u00";
                output += kHexDigits[static_cast<unsigned char>(character) >> 4U];
                output += kHexDigits[static_cast<unsigned char>(character) & 0x0FU];
            } else {
                output += character;
            }
            break;
        }
    }
    output += '"';
}

void AppendCommonJson(
    std::string& output,
    std::string_view description,
    std::string_view category,
    std::string_view example,
    std::string_view documentationAnchor,
    const LibraryApiVersion& version) {
    output += ",\"description\":";
    AppendJsonEscaped(output, description);
    output += ",\"category\":";
    AppendJsonEscaped(output, category);
    output += ",\"example\":";
    AppendJsonEscaped(output, example);
    output += ",\"documentationAnchor\":";
    AppendJsonEscaped(output, documentationAnchor);
    output += ",\"version\":";
    AppendJsonEscaped(output, ToString(version));
}

} // namespace

std::vector<LibraryLuaCompletion> BuildLuaAutocomplete(const ApiManifest& manifest, std::string_view query) {
    std::vector<LibraryLuaCompletion> completions;
    completions.reserve(manifest.catalog.luaBindings.size());
    for (const kb::script::ScriptApiCatalogLuaBinding& binding : manifest.catalog.luaBindings) {
        const kb::script::ScriptApiCatalogFunction* function = manifest.catalog.FindFunction(binding.functionName);
        if (function == nullptr) {
            continue;
        }
        const std::string label = binding.tableName.empty() ? binding.luaName : binding.tableName + "." + binding.luaName;
        const std::string category = binding.tableName.empty() ? "Lua/Global" : "Lua/" + binding.tableName;
        if (!Matches(query, label + " " + category + " " + function->description)) {
            continue;
        }
        completions.push_back(LibraryLuaCompletion{
            .label = label,
            .insertionText = label,
            .description = function->description,
            .category = category,
            .example = LuaExample(*function, binding),
            .documentationAnchor = kb::script::ScriptApiDocumentationAnchor(function->name),
            .version = manifest.version,
        });
    }
    std::ranges::sort(completions, {}, &LibraryLuaCompletion::label);
    return completions;
}

std::vector<LibraryVisualGraphNodeSearchHint> BuildVisualGraphNodeSearchHints(const ApiManifest& manifest, std::string_view query) {
    std::vector<LibraryVisualGraphNodeSearchHint> hints;
    for (const kb::script::ScriptApiCatalogSourceMapEntry& map : manifest.catalog.sourceMap) {
        if (std::ranges::any_of(hints, [&map](const LibraryVisualGraphNodeSearchHint& existing) { return existing.nodeId == map.visualGraphNodeId; })) {
            continue;
        }
        const kb::script::ScriptApiCatalogFunction* function = manifest.catalog.FindFunction(map.functionName);
        if (function == nullptr) {
            continue;
        }
        const std::string displayName = "Function." + function->name;
        if (!Matches(query, displayName + " " + map.visualGraphNodeCategory + " " + function->description)) {
            continue;
        }
        hints.push_back(LibraryVisualGraphNodeSearchHint{
            .nodeId = map.visualGraphNodeId,
            .displayName = displayName,
            .description = function->description,
            .category = map.visualGraphNodeCategory,
            .example = displayName + " CallNative (" + PinNames(function->inputs) + ")",
            .documentationAnchor = map.documentationAnchor,
            .version = manifest.version,
        });
    }
    std::ranges::sort(hints, {}, &LibraryVisualGraphNodeSearchHint::displayName);
    return hints;
}

std::string ToAuthoringHintsJson(const ApiManifest& manifest) {
    const std::vector<LibraryLuaCompletion> luaCompletions = BuildLuaAutocomplete(manifest);
    const std::vector<LibraryVisualGraphNodeSearchHint> nodeHints = BuildVisualGraphNodeSearchHints(manifest);
    std::string output = "{\"luaCompletions\":[";
    for (std::size_t index = 0U; index < luaCompletions.size(); ++index) {
        if (index != 0U) {
            output += ',';
        }
        const LibraryLuaCompletion& completion = luaCompletions[index];
        output += "{\"label\":";
        AppendJsonEscaped(output, completion.label);
        output += ",\"insertionText\":";
        AppendJsonEscaped(output, completion.insertionText);
        AppendCommonJson(output, completion.description, completion.category, completion.example, completion.documentationAnchor, completion.version);
        output += '}';
    }
    output += "],\"visualGraphNodes\":[";
    for (std::size_t index = 0U; index < nodeHints.size(); ++index) {
        if (index != 0U) {
            output += ',';
        }
        const LibraryVisualGraphNodeSearchHint& hint = nodeHints[index];
        output += "{\"nodeId\":";
        AppendJsonEscaped(output, hint.nodeId);
        output += ",\"displayName\":";
        AppendJsonEscaped(output, hint.displayName);
        AppendCommonJson(output, hint.description, hint.category, hint.example, hint.documentationAnchor, hint.version);
        output += '}';
    }
    output += "]}";
    return output;
}

} // namespace kb::library
