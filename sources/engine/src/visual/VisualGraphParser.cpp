#include "visual/VisualGraphParser.hpp"

#include <charconv>
#include <sstream>
#include <string_view>
#include <vector>

namespace kb::visual {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseUInt(std::string_view text, std::uint32_t& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] std::string RemainingText(std::istringstream& stream) {
    std::string rest;
    std::getline(stream, rest);
    return std::string{Trim(rest)};
}

void AddLineError(VisualGraphParseResult& result, std::uint32_t line, std::string message) {
    result.errors.push_back("line " + std::to_string(line) + ": " + std::move(message));
}

} // namespace

VisualGraphParseResult VisualGraphParser::Parse(std::istream& input) {
    VisualGraphParseResult result{};

    std::string lineText;
    std::uint32_t lineNumber = 0;
    while (std::getline(input, lineText)) {
        ++lineNumber;
        const std::string_view line = Trim(StripComment(lineText));
        if (line.empty()) {
            continue;
        }

        std::istringstream stream{std::string{line}};
        std::string command;
        stream >> command;
        if (command == "kbgraph") {
            std::string versionText;
            stream >> versionText;
            if (!ParseUInt(versionText, result.graph.version) || result.graph.version != VisualGraphAsset::kCurrentVersion) {
                AddLineError(result, lineNumber, "unsupported kbgraph version");
            }
        } else if (command == "name") {
            result.graph.name = RemainingText(stream);
        } else if (command == "variable") {
            VisualGraphVariable variable{};
            std::string typeText;
            stream >> variable.name >> typeText;
            variable.defaultValue = RemainingText(stream);
            if (variable.name.empty() || !TryParseVisualGraphValueType(typeText, variable.type)) {
                AddLineError(result, lineNumber, "invalid variable declaration");
            } else {
                result.graph.variables.push_back(std::move(variable));
            }
        } else if (command == "node") {
            VisualGraphNode node{};
            std::string idText;
            std::string kindText;
            stream >> idText >> kindText;
            if (!ParseUInt(idText, node.id) || !TryParseVisualGraphNodeKind(kindText, node.kind)) {
                AddLineError(result, lineNumber, "invalid node declaration");
                continue;
            }
            const std::string symbol = RemainingText(stream);
            if (node.kind == VisualGraphNodeKind::Event) {
                if (!TryParseVisualGraphLifecycleEvent(symbol, node.lifecycle)) {
                    AddLineError(result, lineNumber, "invalid lifecycle event node");
                    continue;
                }
            } else if (node.kind == VisualGraphNodeKind::CustomEvent) {
                if (symbol.empty()) {
                    AddLineError(result, lineNumber, "invalid custom event node");
                    continue;
                }
                node.symbol = symbol;
            } else {
                node.symbol = symbol;
            }
            result.graph.nodes.push_back(std::move(node));
        } else if (command == "pin") {
            VisualGraphPin pin{};
            std::string nodeText;
            std::string directionText;
            std::string typeText;
            stream >> nodeText >> directionText >> pin.name >> typeText;
            if (!ParseUInt(nodeText, pin.nodeId) || !TryParseVisualGraphPinDirection(directionText, pin.direction) || pin.name.empty() ||
                !TryParseVisualGraphValueType(typeText, pin.type)) {
                AddLineError(result, lineNumber, "invalid pin declaration");
            } else {
                result.graph.pins.push_back(std::move(pin));
            }
        } else if (command == "edge") {
            VisualGraphEdge edge{};
            std::vector<std::string> tokens;
            for (std::string token; stream >> token;) {
                tokens.push_back(std::move(token));
            }
            if (tokens.size() == 2U) {
                if (!ParseUInt(tokens[0], edge.fromNode) || !ParseUInt(tokens[1], edge.toNode)) {
                    AddLineError(result, lineNumber, "invalid edge declaration");
                } else {
                    result.graph.edges.push_back(std::move(edge));
                }
            } else if (tokens.size() == 5U) {
                if (!TryParseVisualGraphEdgeKind(tokens[0], edge.kind) || !ParseUInt(tokens[1], edge.fromNode) || !ParseUInt(tokens[3], edge.toNode)) {
                    AddLineError(result, lineNumber, "invalid edge declaration");
                } else {
                    edge.fromPin = std::move(tokens[2]);
                    edge.toPin = std::move(tokens[4]);
                    result.graph.edges.push_back(std::move(edge));
                }
            } else {
                AddLineError(result, lineNumber, "invalid edge declaration");
            }
        } else {
            AddLineError(result, lineNumber, "unknown command '" + command + "'");
        }
    }

    if (result.graph.name.empty()) {
        result.graph.name = "VisualGraph";
    }
    return result;
}

} // namespace kb::visual
