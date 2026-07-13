#include "engine/script/ScriptApiDeclarationParser.hpp"

#include <sstream>
#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool TryParseType(std::string_view text, ScriptValueType& output) noexcept {
    if (text == "Bool" || text == "bool") {
        output = ScriptValueType::Bool;
        return true;
    }
    if (text == "Int" || text == "int") {
        output = ScriptValueType::Int;
        return true;
    }
    if (text == "Float" || text == "float") {
        output = ScriptValueType::Float;
        return true;
    }
    if (text == "String" || text == "string") {
        output = ScriptValueType::String;
        return true;
    }
    if (text == "Entity" || text == "entity") {
        output = ScriptValueType::Entity;
        return true;
    }
    if (text == "Component" || text == "component") {
        output = ScriptValueType::Component;
        return true;
    }
    if (text == "Int64" || text == "int64") {
        output = ScriptValueType::Int64;
        return true;
    }
    if (text == "UInt32" || text == "uint32") {
        output = ScriptValueType::UInt32;
        return true;
    }
    if (text == "Double" || text == "double") {
        output = ScriptValueType::Double;
        return true;
    }
    if (text == "Name" || text == "name") {
        output = ScriptValueType::Name;
        return true;
    }
    if (text == "Guid" || text == "guid") {
        output = ScriptValueType::Guid;
        return true;
    }
    if (text == "Hash" || text == "hash") {
        output = ScriptValueType::Hash;
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<ScriptApiPin> ParsePin(std::string_view token) {
    token = Trim(token);
    const std::size_t separator = token.find(':');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U >= token.size()) {
        return std::nullopt;
    }
    ScriptValueType type = ScriptValueType::Void;
    if (!TryParseType(token.substr(separator + 1U), type)) {
        return std::nullopt;
    }
    return ScriptApiPin{
        .name = std::string{ token.substr(0, separator) },
        .type = type,
        .required = true,
    };
}

[[nodiscard]] std::vector<std::string> SplitTokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::istringstream input{ std::string{ text } };
    std::string token;
    while (input >> token) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

[[nodiscard]] std::optional<std::string_view> ExtractMarkedDeclaration(std::string_view line) noexcept {
    line = Trim(line);
    if (line.starts_with("--")) {
        line = Trim(line.substr(2U));
    } else if (line.starts_with("#")) {
        line = Trim(line.substr(1U));
    } else if (line.starts_with("//")) {
        line = Trim(line.substr(2U));
    }
    if (!line.starts_with("@api")) {
        return std::nullopt;
    }
    if (line.size() > 4U) {
        const char boundary = line[4U];
        if (boundary != ' ' && boundary != '\t' && boundary != '\r' && boundary != '\n') {
            return std::nullopt;
        }
    }
    return Trim(line.substr(4U));
}

} // namespace

std::optional<ScriptApiNameEntry> ScriptApiDeclarationParser::ParseDeclaration(
    std::string_view declaration,
    std::string owner,
    bool functionDeclaresProvider) {
    const std::vector<std::string> tokens = SplitTokens(declaration);
    if (tokens.size() < 2U) {
        return std::nullopt;
    }

    if (tokens[0] == "shared") {
        ScriptValueType type = ScriptValueType::Void;
        if (tokens.size() >= 3U && !TryParseType(tokens[2], type)) {
            return std::nullopt;
        }
        return ScriptApiNameEntry{
            .kind = ScriptApiNameKind::SharedKey,
            .name = tokens[1],
            .owner = std::move(owner),
            .valueType = type,
            .hasValueTypeContract = type != ScriptValueType::Void,
        };
    }

    if (tokens[0] == "event") {
        ScriptApiNameEntry entry{
            .kind = ScriptApiNameKind::Event,
            .name = tokens[1],
            .owner = std::move(owner),
            .hasInputContract = true,
        };
        for (std::size_t index = 2U; index < tokens.size(); ++index) {
            std::optional<ScriptApiPin> pin = ParsePin(tokens[index]);
            if (!pin.has_value()) {
                return std::nullopt;
            }
            entry.inputs.push_back(std::move(*pin));
        }
        return entry;
    }

    if (tokens[0] == "function") {
        ScriptApiNameEntry entry{
            .kind = ScriptApiNameKind::Function,
            .name = tokens[1],
            .owner = std::move(owner),
            .hasInputContract = true,
            .hasOutputContract = true,
            .declaresProvider = functionDeclaresProvider,
        };
        bool outputs = false;
        for (std::size_t index = 2U; index < tokens.size(); ++index) {
            if (tokens[index] == "->") {
                outputs = true;
                continue;
            }
            std::optional<ScriptApiPin> pin = ParsePin(tokens[index]);
            if (!pin.has_value()) {
                return std::nullopt;
            }
            if (outputs) {
                pin->required = false;
                entry.outputs.push_back(std::move(*pin));
            } else {
                entry.inputs.push_back(std::move(*pin));
            }
        }
        return entry;
    }

    return std::nullopt;
}

ScriptApiDeclarationParseResult ScriptApiDeclarationParser::CollectMarkedDeclarations(
    std::string_view source,
    std::string owner,
    bool functionDeclaresProvider) {
    ScriptApiDeclarationParseResult result{};
    std::istringstream input{ std::string{ source } };
    std::string rawLine;
    std::size_t lineNumber = 0U;
    while (std::getline(input, rawLine)) {
        ++lineNumber;
        const std::optional<std::string_view> declaration = ExtractMarkedDeclaration(rawLine);
        if (!declaration.has_value()) {
            continue;
        }
        std::optional<ScriptApiNameEntry> entry = ParseDeclaration(*declaration, owner, functionDeclaresProvider);
        if (!entry.has_value()) {
            result.errors.push_back("invalid script API declaration at line " + std::to_string(lineNumber));
            continue;
        }
        result.entries.push_back(std::move(*entry));
    }
    return result;
}

} // namespace kb::script
