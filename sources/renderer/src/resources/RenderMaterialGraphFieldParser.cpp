#include "resources/RenderMaterialGraphFieldParser.hpp"

#include <charconv>
#include <sstream>
#include <string>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] bool ParseUInt32(std::string_view text, std::uint32_t& output) noexcept {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseInt32(std::string_view text, std::int32_t& output) noexcept {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

void AddDiagnostic(
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics,
    RenderMaterialAssetParseDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text) {
    diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphVersion(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::uint32_t version = 0U;
    if (!ParseUInt32(rest, version) || version == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphVersion", "Invalid material graph document version.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (version != kRenderMaterialGraphDocumentVersion) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::UnsupportedGraphVersion, line, "graphVersion", "Unsupported material graph document version " + std::to_string(version) + ".", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.documentVersion = version;
    graph.hasExplicitDocumentVersion = true;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphNode(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string idText;
    std::string kindText;
    std::string xText;
    std::string yText;
    if (!(stream >> idText >> kindText >> xText >> yText)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphNode", "Material graph node requires id, kind, x and y.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphNode node{};
    if (!ParseUInt32(idText, node.id) || node.id == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphNode", "Material graph node id must be a positive unsigned integer.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (FindRenderMaterialGraphNode(graph, node.id) != nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::DuplicateGraphNode, line, "graphNode", "Material graph node id is duplicated.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const std::optional<RenderMaterialGraphNodeKind> kind = ParseRenderMaterialGraphNodeKind(kindText);
    if (!kind.has_value()) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphNode", "Material graph node kind is not supported.", kindText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseInt32(xText, node.positionX) || !ParseInt32(yText, node.positionY)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphNode", "Material graph node position must use signed integer coordinates.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    node.kind = *kind;
    graph.nodes.push_back(node);
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLink(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string fromNodeText;
    std::string fromPin;
    std::string toNodeText;
    std::string toPin;
    if (!(stream >> fromNodeText >> fromPin >> toNodeText >> toPin)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link requires source node, source pin, target node and target pin.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphLink link{};
    if (!ParseUInt32(fromNodeText, link.fromNodeId) || !ParseUInt32(toNodeText, link.toNodeId) || link.fromNodeId == 0U || link.toNodeId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link node ids must be positive unsigned integers.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
    const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
    if (fromNode == nullptr || toNode == nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link references an undeclared node.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!IsRenderMaterialGraphOutputPin(fromNode->kind, fromPin)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid source pin.", fromPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!IsRenderMaterialGraphInputPin(toNode->kind, toPin)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid target pin.", toPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    link.fromPin = std::move(fromPin);
    link.toPin = std::move(toPin);
    graph.links.push_back(std::move(link));
    return RenderMaterialGraphFieldParseResult::Parsed;
}

} // namespace

RenderMaterialGraphFieldParseResult RenderMaterialGraphFieldParser::Apply(
    std::string_view keyword,
    std::string_view rest,
    std::size_t line,
    RenderMaterialAssetData& asset,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    if (keyword == "graphVersion") {
        return ParseGraphVersion(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphNode") {
        return ParseGraphNode(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLink") {
        return ParseGraphLink(rest, line, asset.graph, diagnostics);
    }
    return RenderMaterialGraphFieldParseResult::Unknown;
}

} // namespace kb::render
