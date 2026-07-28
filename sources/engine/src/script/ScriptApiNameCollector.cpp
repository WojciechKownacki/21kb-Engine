#include "engine/script/ScriptApiNameCollector.hpp"

#include "engine/script/ScriptApiDeclarationParser.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/library/EngineLibraryAssetRef.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

struct LuaCallPattern final {
    std::string functionName;
    ScriptApiNameKind kind = ScriptApiNameKind::Event;
    std::size_t stringArgumentIndex = 0U;
};

const std::array kLuaCallPatterns{
    LuaCallPattern{ .functionName = "Emit", .kind = ScriptApiNameKind::Event, .stringArgumentIndex = 0U },
    LuaCallPattern{ .functionName = "EmitTo", .kind = ScriptApiNameKind::Event, .stringArgumentIndex = 1U },
    LuaCallPattern{ .functionName = "SetShared", .kind = ScriptApiNameKind::SharedKey, .stringArgumentIndex = 0U },
    LuaCallPattern{ .functionName = "GetShared", .kind = ScriptApiNameKind::SharedKey, .stringArgumentIndex = 0U },
    LuaCallPattern{ .functionName = "HasShared", .kind = ScriptApiNameKind::SharedKey, .stringArgumentIndex = 0U },
    LuaCallPattern{ .functionName = "RemoveShared", .kind = ScriptApiNameKind::SharedKey, .stringArgumentIndex = 0U },
    LuaCallPattern{ .functionName = "CallFunction", .kind = ScriptApiNameKind::Function, .stringArgumentIndex = 0U },
};

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::vector<LuaCallPattern> CollectLuaCallPatterns(std::string_view source) {
    std::vector<LuaCallPattern> patterns{ kLuaCallPatterns.begin(), kLuaCallPatterns.end() };
    std::istringstream input{ std::string{ source } };
    std::string rawLine;
    while (std::getline(input, rawLine)) {
        std::string_view line = Trim(rawLine);
        if (!line.starts_with("local ")) {
            continue;
        }
        line = Trim(line.substr(6U));
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string alias{ Trim(line.substr(0U, equals)) };
        const std::string_view target = Trim(line.substr(equals + 1U));
        for (const LuaCallPattern& pattern : kLuaCallPatterns) {
            if (target == pattern.functionName && !alias.empty()) {
                patterns.push_back(LuaCallPattern{
                    .functionName = alias,
                    .kind = pattern.kind,
                    .stringArgumentIndex = pattern.stringArgumentIndex,
                });
                break;
            }
        }
    }
    return patterns;
}

[[nodiscard]] bool IsIdentifierChar(char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
}

[[nodiscard]] std::size_t SkipWhitespace(std::string_view source, std::size_t offset) noexcept {
    while (offset < source.size() && (source[offset] == ' ' || source[offset] == '\t' || source[offset] == '\r' || source[offset] == '\n')) {
        ++offset;
    }
    return offset;
}

[[nodiscard]] std::size_t SkipLuaArgument(std::string_view source, std::size_t offset) noexcept {
    offset = SkipWhitespace(source, offset);
    while (offset < source.size() && source[offset] != ',' && source[offset] != ')') {
        if (source[offset] == '"' || source[offset] == '\'') {
            const char quote = source[offset++];
            while (offset < source.size()) {
                if (source[offset] == '\\') {
                    offset += 2U;
                    continue;
                }
                if (source[offset++] == quote) {
                    break;
                }
            }
            continue;
        }
        ++offset;
    }
    return offset;
}

[[nodiscard]] std::optional<std::string> ReadLuaStringLiteral(std::string_view source, std::size_t& offset) {
    offset = SkipWhitespace(source, offset);
    if (offset >= source.size() || (source[offset] != '"' && source[offset] != '\'')) {
        return std::nullopt;
    }
    const char quote = source[offset++];
    std::string value;
    while (offset < source.size()) {
        const char current = source[offset++];
        if (current == quote) {
            return value;
        }
        if (current == '\\' && offset < source.size()) {
            value.push_back(source[offset++]);
            continue;
        }
        value.push_back(current);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ScriptValueType> InferLuaLiteralType(std::string_view source, std::size_t offset) {
    offset = SkipWhitespace(source, offset);
    if (offset >= source.size()) {
        return std::nullopt;
    }
    if (source[offset] == '"' || source[offset] == '\'') {
        return ScriptValueType::String;
    }
    if (source.substr(offset, 4U) == "true" && (offset + 4U >= source.size() || !IsIdentifierChar(source[offset + 4U]))) {
        return ScriptValueType::Bool;
    }
    if (source.substr(offset, 5U) == "false" && (offset + 5U >= source.size() || !IsIdentifierChar(source[offset + 5U]))) {
        return ScriptValueType::Bool;
    }
    if (source[offset] == '-' || source[offset] == '+' || std::isdigit(static_cast<unsigned char>(source[offset])) != 0) {
        bool hasDigits = false;
        bool hasDecimal = false;
        if (source[offset] == '-' || source[offset] == '+') {
            ++offset;
        }
        while (offset < source.size() && (std::isdigit(static_cast<unsigned char>(source[offset])) != 0 || source[offset] == '.')) {
            hasDigits = hasDigits || std::isdigit(static_cast<unsigned char>(source[offset])) != 0;
            hasDecimal = hasDecimal || source[offset] == '.';
            ++offset;
        }
        if (hasDigits) {
            return hasDecimal ? ScriptValueType::Float : ScriptValueType::Int;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool IsLuaCallAt(std::string_view source, std::size_t offset, std::string_view functionName) noexcept {
    if (offset + functionName.size() > source.size() || source.substr(offset, functionName.size()) != functionName) {
        return false;
    }
    if (offset > 0U && IsIdentifierChar(source[offset - 1U])) {
        return false;
    }
    const std::size_t afterName = offset + functionName.size();
    return afterName >= source.size() || !IsIdentifierChar(source[afterName]);
}

[[nodiscard]] ScriptValueType ToScriptValueType(kb::visual::VisualGraphValueType type) noexcept {
    switch (type) {
    case kb::visual::VisualGraphValueType::Bool:
        return ScriptValueType::Bool;
    case kb::visual::VisualGraphValueType::Int:
        return ScriptValueType::Int;
    case kb::visual::VisualGraphValueType::Float:
        return ScriptValueType::Float;
    case kb::visual::VisualGraphValueType::String:
        return ScriptValueType::String;
    case kb::visual::VisualGraphValueType::Entity:
        return ScriptValueType::Entity;
    case kb::visual::VisualGraphValueType::Component:
        return ScriptValueType::Component;
    case kb::visual::VisualGraphValueType::Int64:
        return ScriptValueType::Int64;
    case kb::visual::VisualGraphValueType::UInt32:
        return ScriptValueType::UInt32;
    case kb::visual::VisualGraphValueType::Double:
        return ScriptValueType::Double;
    case kb::visual::VisualGraphValueType::Name:
        return ScriptValueType::Name;
    case kb::visual::VisualGraphValueType::Guid:
        return ScriptValueType::Guid;
    case kb::visual::VisualGraphValueType::Hash:
        return ScriptValueType::Hash;
    case kb::visual::VisualGraphValueType::Void:
        break;
    }
    return ScriptValueType::Void;
}

[[nodiscard]] bool IsExecutionPin(std::string_view name, kb::visual::VisualGraphValueType type) noexcept {
    return type == kb::visual::VisualGraphValueType::Void || name == "exec" || name == "then" || name == "true" || name == "false";
}

[[nodiscard]] std::vector<ScriptApiPin> CollectNodePins(
    const kb::visual::VisualGraphAsset& graph,
    const kb::visual::VisualGraphNode& node,
    kb::visual::VisualGraphPinDirection direction) {
    std::vector<ScriptApiPin> pins;
    for (const kb::visual::VisualGraphPin& pin : graph.pins) {
        if (pin.nodeId != node.id || pin.direction != direction || IsExecutionPin(pin.name, pin.type)) {
            continue;
        }
        pins.push_back(ScriptApiPin{
            .name = pin.name,
            .type = ToScriptValueType(pin.type),
            .required = direction == kb::visual::VisualGraphPinDirection::Input,
        });
    }
    return pins;
}

void AddCollectionError(ScriptApiNameCollectionResult& result, std::string message);

void CollectLuaSource(ScriptApiNameCollectionResult& result, std::string_view source, std::string owner) {
    const ScriptApiDeclarationParseResult declarations = ScriptApiDeclarationParser::CollectMarkedDeclarations(source, owner, false);
    for (const ScriptApiNameEntry& entry : declarations.entries) {
        static_cast<void>(result.names.RegisterEntry(entry));
    }
    for (const std::string& error : declarations.errors) {
        AddCollectionError(result, error);
    }
    const std::vector<LuaCallPattern> callPatterns = CollectLuaCallPatterns(source);
    for (std::size_t offset = 0U; offset < source.size(); ++offset) {
        for (const LuaCallPattern& pattern : callPatterns) {
            if (!IsLuaCallAt(source, offset, pattern.functionName)) {
                continue;
            }
            std::size_t cursor = SkipWhitespace(source, offset + pattern.functionName.size());
            if (cursor >= source.size() || source[cursor] != '(') {
                continue;
            }
            ++cursor;
            for (std::size_t argument = 0U; argument < pattern.stringArgumentIndex && cursor < source.size(); ++argument) {
                cursor = SkipLuaArgument(source, cursor);
                if (cursor < source.size() && source[cursor] == ',') {
                    ++cursor;
                }
            }
            std::optional<std::string> literal = ReadLuaStringLiteral(source, cursor);
            if (literal.has_value()) {
                if (pattern.functionName == "SetShared") {
                    cursor = SkipWhitespace(source, cursor);
                    if (cursor < source.size() && source[cursor] == ',') {
                        ++cursor;
                    }
                    const std::optional<ScriptValueType> valueType = InferLuaLiteralType(source, cursor);
                    if (valueType.has_value()) {
                        static_cast<void>(result.names.RegisterSharedKey(std::move(*literal), *valueType, owner));
                        continue;
                    }
                }
                static_cast<void>(result.names.Register(pattern.kind, std::move(*literal), owner));
            }
        }
    }
}

void CollectNativeDescriptor(ScriptApiNameRegistry& registry, const NativeBehaviourDescriptor& descriptor, std::string owner) {
    for (ScriptApiNameEntry entry : descriptor.apiDeclarations) {
        if (entry.owner.empty()) {
            entry.owner = owner;
        }
        static_cast<void>(registry.RegisterEntry(std::move(entry)));
    }
}

void CollectVisualGraph(ScriptApiNameRegistry& registry, const kb::visual::VisualGraphAsset& graph, std::string owner) {
    for (const kb::visual::VisualGraphNode& node : graph.nodes) {
        if (node.kind == kb::visual::VisualGraphNodeKind::EmitEvent) {
            const std::vector<ScriptApiPin> payload = CollectNodePins(graph, node, kb::visual::VisualGraphPinDirection::Input);
            static_cast<void>(registry.RegisterEvent(node.symbol, payload, owner));
            continue;
        }
        if (node.kind == kb::visual::VisualGraphNodeKind::CustomEvent) {
            const std::vector<ScriptApiPin> payload = CollectNodePins(graph, node, kb::visual::VisualGraphPinDirection::Output);
            static_cast<void>(registry.RegisterEvent(node.symbol, payload, owner));
            continue;
        }
        static constexpr std::string_view kFunctionPrefix = "Function.";
        if (node.kind == kb::visual::VisualGraphNodeKind::CallNative && node.symbol.starts_with(kFunctionPrefix)) {
            const std::vector<ScriptApiPin> inputs = CollectNodePins(graph, node, kb::visual::VisualGraphPinDirection::Input);
            const std::vector<ScriptApiPin> outputs = CollectNodePins(graph, node, kb::visual::VisualGraphPinDirection::Output);
            static_cast<void>(registry.RegisterFunction(std::string{ node.symbol.substr(kFunctionPrefix.size()) }, inputs, outputs, owner));
        }
    }
}

void AddCollectionError(ScriptApiNameCollectionResult& result, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(kb::visual::VisualGraphDiagnostics::Error(kb::visual::VisualGraphDiagnosticStage::ApiNameValidation, std::move(message)));
}

} // namespace

ScriptApiNameCollectionResult ScriptApiNameCollector::CollectProjectAssets(kb::assets::AssetManager& assets) {
    ScriptApiNameCollectionResult result{};
    for (const kb::assets::AssetMetadata& metadata : assets.Registry().All()) {
        const std::string owner = kb::assets::NormalizeAssetPath(metadata.virtualPath);
        if (metadata.type == ScriptAssetTypes::LuaScript) {
            const kb::assets::AssetHandle<LuaScriptAsset> script = assets.Load<LuaScriptAsset>(metadata.id);
            if (!script.IsLoaded()) {
                AddCollectionError(result, assets.LastError().empty() ? "Lua script could not be loaded for API name collection" : assets.LastError());
                continue;
            }
            CollectLuaSource(result, script->source, owner);
            for (const ScriptApiPin& exposedVariable : script->exposedVariables) {
                static_cast<void>(result.names.RegisterExposedVariable(exposedVariable.name, exposedVariable.type, owner));
            }
            continue;
        }
        if (metadata.type == ScriptAssetTypes::NativeBehaviour) {
            const kb::assets::AssetHandle<NativeBehaviourDescriptor> descriptor = assets.Load<NativeBehaviourDescriptor>(metadata.id);
            if (!descriptor.IsLoaded()) {
                AddCollectionError(result, assets.LastError().empty() ? "Native behaviour descriptor could not be loaded for API name collection" : assets.LastError());
                continue;
            }
            CollectNativeDescriptor(result.names, *descriptor.Get(), owner);
            continue;
        }
        if (metadata.type == "VisualGraph") {
            const kb::library::GraphRef graph = assets.Load<kb::visual::VisualGraphAsset>(metadata.id);
            if (!graph.IsLoaded()) {
                AddCollectionError(result, assets.LastError().empty() ? "Visual graph could not be loaded for API name collection" : assets.LastError());
                continue;
            }
            CollectVisualGraph(result.names, *graph.Get(), owner);
        }
    }
    return result;
}

} // namespace kb::script
