#include "engine/script/ScriptAssetLoader.hpp"

#include "engine/script/ScriptApiDeclarationParser.hpp"
#include "engine/script/ScriptAsset.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] std::string ReadWholeFile(const std::filesystem::path& path, std::string& error) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        error = "Script asset file could not be opened";
        return {};
    }

    std::string content{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (!input.good() && !input.eof()) {
        error = "Script asset file could not be read";
        return {};
    }
    return content;
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool StartsWithComment(std::string_view line) noexcept {
    return line.starts_with('#') || line.starts_with("//");
}

[[nodiscard]] bool ParseBool(std::string_view value, bool fallback) noexcept {
    value = Trim(value);
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

[[nodiscard]] bool TryParseScriptValueType(std::string_view value, ScriptValueType& output) noexcept {
    value = Trim(value);
    if (value == "Bool" || value == "bool") {
        output = ScriptValueType::Bool;
        return true;
    }
    if (value == "Int" || value == "int") {
        output = ScriptValueType::Int;
        return true;
    }
    if (value == "Float" || value == "float") {
        output = ScriptValueType::Float;
        return true;
    }
    if (value == "String" || value == "string") {
        output = ScriptValueType::String;
        return true;
    }
    if (value == "Entity" || value == "entity") {
        output = ScriptValueType::Entity;
        return true;
    }
    if (value == "Component" || value == "component") {
        output = ScriptValueType::Component;
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<ScriptValue> TryParseScriptValue(std::string_view value, ScriptValueType type) {
    value = Trim(value);
    if (value.empty()) {
        return std::nullopt;
    }
    switch (type) {
    case ScriptValueType::Bool:
        if (value == "true" || value == "1") {
            return ScriptValue{ true };
        }
        if (value == "false" || value == "0") {
            return ScriptValue{ false };
        }
        return std::nullopt;
    case ScriptValueType::Int:
    {
        int parsedValue = 0;
        const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional<ScriptValue>{ ScriptValue{ parsedValue } } : std::nullopt;
    }
    case ScriptValueType::Float:
    {
        float parsedValue = 0.0F;
        const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional<ScriptValue>{ ScriptValue{ parsedValue } } : std::nullopt;
    }
    case ScriptValueType::String:
        if ((value.size() >= 2U && value.front() == '"' && value.back() == '"') ||
            (value.size() >= 2U && value.front() == '\'' && value.back() == '\'')) {
            return ScriptValue{ std::string{ value.substr(1U, value.size() - 2U) } };
        }
        return ScriptValue{ std::string{ value } };
    case ScriptValueType::Entity:
    {
        std::uint64_t parsedValue = 0U;
        const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional<ScriptValue>{ ScriptValue{ parsedValue, ScriptValueType::Entity } } : std::nullopt;
    }
    case ScriptValueType::Component:
    {
        std::uint64_t parsedValue = 0U;
        const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional<ScriptValue>{ ScriptValue{ parsedValue, ScriptValueType::Component } } : std::nullopt;
    }
    case ScriptValueType::Void:
        break;
    }
        return std::nullopt;
}

[[nodiscard]] ScriptValue DefaultScriptValue(ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Bool:
        return ScriptValue{ false };
    case ScriptValueType::Int:
        return ScriptValue{ 0 };
    case ScriptValueType::Float:
        return ScriptValue{ 0.0F };
    case ScriptValueType::String:
        return ScriptValue{ std::string{} };
    case ScriptValueType::Entity:
        return ScriptValue{ 0U, ScriptValueType::Entity };
    case ScriptValueType::Component:
        return ScriptValue{ 0U, ScriptValueType::Component };
    case ScriptValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] bool SplitKeyValue(std::string_view line, std::string_view& key, std::string_view& value) noexcept {
    const std::size_t equals = line.find('=');
    if (equals != std::string_view::npos) {
        key = Trim(line.substr(0, equals));
        value = Trim(line.substr(equals + 1));
        return !key.empty();
    }

    const std::size_t space = line.find_first_of(" \t");
    if (space == std::string_view::npos) {
        key = Trim(line);
        value = {};
        return !key.empty();
    }

    key = Trim(line.substr(0, space));
    value = Trim(line.substr(space + 1));
    return !key.empty();
}

[[nodiscard]] std::optional<std::string_view> ExtractLuaDirective(std::string_view line, std::string_view directive) noexcept {
    line = Trim(line);
    if (!line.starts_with("--")) {
        return std::nullopt;
    }
    line = Trim(line.substr(2U));
    if (!line.starts_with(directive)) {
        return std::nullopt;
    }
    if (line.size() > directive.size()) {
        const char boundary = line[directive.size()];
        if (boundary != ' ' && boundary != '\t' && boundary != '\r' && boundary != '\n') {
            return std::nullopt;
        }
    }
    return Trim(line.substr(directive.size()));
}

void ParseLuaAssetMetadata(LuaScriptAsset& asset) {
    std::istringstream input{ asset.source };
    std::string rawLine;
    while (std::getline(input, rawLine)) {
        if (const std::optional<std::string_view> importName = ExtractLuaDirective(rawLine, "@import"); importName.has_value() && !importName->empty()) {
            asset.imports.emplace_back(*importName);
            continue;
        }
        const std::optional<std::string_view> exposed = ExtractLuaDirective(rawLine, "@expose");
        if (!exposed.has_value()) {
            continue;
        }
        std::string_view declaration = *exposed;
        std::string_view defaultText;
        const std::size_t defaultSeparator = declaration.find('=');
        if (defaultSeparator != std::string_view::npos) {
            defaultText = Trim(declaration.substr(defaultSeparator + 1U));
            declaration = Trim(declaration.substr(0U, defaultSeparator));
        }

        std::istringstream exposedInput{ std::string{ declaration } };
        std::string name;
        std::string typeName;
        exposedInput >> name >> typeName;
        ScriptValueType type = ScriptValueType::Void;
        if (!name.empty() && TryParseScriptValueType(typeName, type)) {
            asset.exposedVariables.push_back(ScriptApiPin{
                .name = std::move(name),
                .type = type,
                .required = false,
            });
            if (!defaultText.empty()) {
                const std::optional<ScriptValue> parsedDefault = TryParseScriptValue(defaultText, type);
                asset.exposedVariableDefaults.push_back(parsedDefault.value_or(DefaultScriptValue(type)));
                asset.exposedVariableHasDefault.push_back(parsedDefault.has_value());
            } else {
                asset.exposedVariableDefaults.push_back(DefaultScriptValue(type));
                asset.exposedVariableHasDefault.push_back(false);
            }
        }
    }
}

struct NativeBehaviourDescriptorParseResult {
    NativeBehaviourDescriptor descriptor;
    std::vector<std::string> errors;
};

[[nodiscard]] NativeBehaviourDescriptorParseResult ParseNativeBehaviourDescriptor(std::string_view source) {
    NativeBehaviourDescriptorParseResult result{};
    std::istringstream input{ std::string{ source } };
    std::string rawLine;
    std::size_t lineNumber = 0U;
    while (std::getline(input, rawLine)) {
        ++lineNumber;
        const std::string_view line = Trim(rawLine);
        if (line.empty() || StartsWithComment(line)) {
            continue;
        }

        std::string_view key;
        std::string_view value;
        if (!SplitKeyValue(line, key, value)) {
            continue;
        }

        if (key == "name") {
            result.descriptor.name.assign(value);
        } else if (key == "symbol" || key == "type") {
            result.descriptor.symbol.assign(value);
        } else if (key == "module" || key == "dll" || key == "library") {
            result.descriptor.modulePath = std::filesystem::path{ std::string{ value } };
        } else if (key == "entry" || key == "entry_point") {
            result.descriptor.entryPoint.assign(value);
        } else if (key == "build") {
            result.descriptor.build.enabled = true;
            result.descriptor.build.command.assign(value);
        } else if (key == "build_working_directory" || key == "build_cwd") {
            result.descriptor.build.workingDirectory = std::filesystem::path{ std::string{ value } };
        } else if (key == "shadow_copy") {
            result.descriptor.shadowCopy = ParseBool(value, true);
        } else if (key == "api") {
            std::optional<ScriptApiNameEntry> declaration = ScriptApiDeclarationParser::ParseDeclaration(value, result.descriptor.name, true);
            if (declaration.has_value()) {
                result.descriptor.apiDeclarations.push_back(std::move(*declaration));
            } else {
                result.errors.push_back("Native behaviour descriptor has invalid API declaration at line " + std::to_string(lineNumber));
            }
        }
    }
    return result;
}

} // namespace

std::string_view LuaScriptAssetLoader::Type() const noexcept {
    return ScriptAssetTypes::LuaScript;
}

std::type_index LuaScriptAssetLoader::PayloadType() const noexcept {
    return typeid(LuaScriptAsset);
}

std::vector<std::string> LuaScriptAssetLoader::Extensions() const {
    return { ".lua" };
}

kb::assets::AssetLoadResult LuaScriptAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    std::string source = ReadWholeFile(request.resolvedPath, error);
    if (!error.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }

    LuaScriptAsset asset{ .source = std::move(source) };
    ParseLuaAssetMetadata(asset);
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<LuaScriptAsset>(std::move(asset)),
        .error = {},
    };
}

std::string_view NativeBehaviourDescriptorAssetLoader::Type() const noexcept {
    return ScriptAssetTypes::NativeBehaviour;
}

std::type_index NativeBehaviourDescriptorAssetLoader::PayloadType() const noexcept {
    return typeid(NativeBehaviourDescriptor);
}

std::vector<std::string> NativeBehaviourDescriptorAssetLoader::Extensions() const {
    return { ".native", ".kbnative" };
}

kb::assets::AssetLoadResult NativeBehaviourDescriptorAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    std::string source = ReadWholeFile(request.resolvedPath, error);
    if (!error.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }

    NativeBehaviourDescriptorParseResult parsed = ParseNativeBehaviourDescriptor(source);
    if (!parsed.errors.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = parsed.errors.front() };
    }
    NativeBehaviourDescriptor descriptor = std::move(parsed.descriptor);
    if (descriptor.symbol.empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Native behaviour descriptor requires a non-empty symbol field" };
    }
    if (descriptor.name.empty()) {
        descriptor.name = request.metadata.name;
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<NativeBehaviourDescriptor>(std::move(descriptor)),
        .error = {},
    };
}

} // namespace kb::script
