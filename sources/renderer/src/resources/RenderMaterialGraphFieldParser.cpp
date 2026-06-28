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

[[nodiscard]] bool ParseUInt64(std::string_view text, std::uint64_t& output) noexcept {
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

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& output) noexcept {
    if (text == "true" || text == "1") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0") {
        output = false;
        return true;
    }
    return false;
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

[[nodiscard]] std::string DecodeToken(std::string_view value) {
    std::string decoded;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2U < value.size()) {
            const std::string_view code = value.substr(index + 1U, 2U);
            if (code == "20") {
                decoded += ' ';
                index += 2U;
                continue;
            }
            if (code == "09") {
                decoded += '\t';
                index += 2U;
                continue;
            }
            if (code == "25") {
                decoded += '%';
                index += 2U;
                continue;
            }
            if (code == "23") {
                decoded += '#';
                index += 2U;
                continue;
            }
        }
        decoded += value[index];
    }
    return decoded;
}

[[nodiscard]] bool ParseParameterGroup(std::string_view text, RenderMaterialParameterGroup& output) noexcept {
    if (text == "Core") {
        output = RenderMaterialParameterGroup::Core;
        return true;
    }
    if (text == "Surface") {
        output = RenderMaterialParameterGroup::Surface;
        return true;
    }
    if (text == "Advanced") {
        output = RenderMaterialParameterGroup::Advanced;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseTextureColorSpace(std::string_view text, RenderMaterialTextureColorSpace& output) noexcept {
    if (text == "Srgb" || text == "sRGB") {
        output = RenderMaterialTextureColorSpace::Srgb;
        return true;
    }
    if (text == "Linear") {
        output = RenderMaterialTextureColorSpace::Linear;
        return true;
    }
    if (text == "Unknown") {
        output = RenderMaterialTextureColorSpace::Unknown;
        return true;
    }
    return false;
}

[[nodiscard]] RenderMaterialGraphNode* FindMutableGraphNode(RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    for (RenderMaterialGraphNode& node : graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
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

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphMaterialDomain(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    if (rest != "surface") {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphMaterialDomain", "Unsupported material graph domain.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.materialDomain = std::string{ rest };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphShadingModel(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    if (rest != "lit") {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphShadingModel", "Unsupported material graph shading model.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.shadingModel = std::string{ rest };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphStorageModel(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    if (rest != "inline-kbmat" && rest != "material-graph-asset") {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphStorageModel", "Unsupported material graph storage model.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.storageModel = std::string{ rest };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphDiagnosticSchemaVersion(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::uint32_t version = 0U;
    if (!ParseUInt32(rest, version) || version == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphDiagnosticSchemaVersion", "Invalid material graph diagnostic schema version.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.diagnosticSchemaVersion = version;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphPersistCompileDiagnostics(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    bool persist = false;
    if (!ParseBool(rest, persist)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphPersistCompileDiagnostics", "Invalid material graph diagnostics persistence flag.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.persistCompileDiagnostics = persist;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphArtifactFailurePolicy(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    const std::optional<RenderMaterialGraphArtifactFailurePolicy> policy = ParseRenderMaterialGraphArtifactFailurePolicy(rest);
    if (!policy.has_value()) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphArtifactFailurePolicy", "Invalid material graph artifact failure policy.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.artifactFailurePolicy = *policy;
    graph.hasExplicitArtifactFailurePolicy = true;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLastGoodArtifactAssetId(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::uint64_t assetId = 0U;
    if (!ParseUInt64(rest, assetId) || assetId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphLastGoodArtifactAssetId", "Invalid material graph last-good artifact asset id.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.lastGoodArtifact.assetId = assetId;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLastGoodArtifactHash(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::uint64_t contentHash = 0U;
    if (!ParseUInt64(rest, contentHash) || contentHash == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphLastGoodArtifactHash", "Invalid material graph last-good artifact content hash.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.lastGoodArtifact.contentHash = contentHash;
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

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphParameter(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string nodeIdText;
    std::string stableId;
    std::string displayName;
    std::string groupText;
    std::string defaultValue;
    std::string rangeMinText;
    std::string rangeMaxText;
    std::string textureRole;
    std::string colorSpaceText;
    std::string overrideText;
    std::string editorOrderText;
    std::string description;
    if (!(stream >> nodeIdText >> stableId >> displayName >> groupText >> defaultValue >> rangeMinText >> rangeMaxText >>
            textureRole >> colorSpaceText >> overrideText >> editorOrderText >> description)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter metadata requires node id, stable id, display name, group, default, range, texture role, color space, override flag, editor order and description.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    std::uint32_t nodeId = 0U;
    if (!ParseUInt32(nodeIdText, nodeId) || nodeId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter node id must be a positive unsigned integer.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    RenderMaterialGraphNode* node = FindMutableGraphNode(graph, nodeId);
    const bool supportsMetadata = node != nullptr &&
        (IsRenderMaterialGraphParameterNode(node->kind) ||
            node->kind == RenderMaterialGraphNodeKind::TextureSample ||
            node->kind == RenderMaterialGraphNodeKind::ConstantScalar ||
            node->kind == RenderMaterialGraphNodeKind::ConstantVector2 ||
            node->kind == RenderMaterialGraphNodeKind::ConstantVector ||
            node->kind == RenderMaterialGraphNodeKind::ConstantColor);
    if (!supportsMetadata) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter metadata must reference an existing parameter, constant or texture sample node.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialParameterGroup group{};
    RenderMaterialTextureColorSpace colorSpace{};
    bool overrideSupported = true;
    std::uint32_t editorOrder = 0U;
    if (!ParseParameterGroup(groupText, group) ||
        !ParseTextureColorSpace(colorSpaceText, colorSpace) ||
        !ParseBool(overrideText, overrideSupported) ||
        !ParseUInt32(editorOrderText, editorOrder)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter metadata has invalid group, color-space, override flag or editor order.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    bool hasRange = false;
    float rangeMin = 0.0F;
    float rangeMax = 1.0F;
    if (rangeMinText != "_" || rangeMaxText != "_") {
        if (!ParseFloat(rangeMinText, rangeMin) || !ParseFloat(rangeMaxText, rangeMax) || rangeMin > rangeMax) {
            AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter metadata has an invalid range.", std::string{ rest });
            return RenderMaterialGraphFieldParseResult::Failed;
        }
        hasRange = true;
    }

    node->parameter = RenderMaterialGraphParameterMetadata{
        .stableId = stableId == "_" ? std::string{} : DecodeToken(stableId),
        .displayName = displayName == "_" ? std::string{} : DecodeToken(displayName),
        .group = group,
        .defaultValueHint = defaultValue == "_" ? std::string{} : DecodeToken(defaultValue),
        .hasRange = hasRange,
        .rangeMin = rangeMin,
        .rangeMax = rangeMax,
        .textureRole = textureRole == "_" ? std::string{} : DecodeToken(textureRole),
        .expectedTextureColorSpace = colorSpace,
        .overrideSupported = overrideSupported,
        .editorOrder = editorOrder,
        .description = description == "_" ? std::string{} : DecodeToken(description),
    };
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
    if (!AreRenderMaterialGraphPinsCompatible(fromNode->kind, fromPin, toNode->kind, toPin)) {
        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(toNode->kind, toPin, false);
        AddDiagnostic(
            diagnostics,
            RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink,
            line,
            "graphLink",
            "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".",
            std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    link.fromPinId = RenderMaterialGraphStablePinId(fromNode->kind, fromPin, true);
    link.toPinId = RenderMaterialGraphStablePinId(toNode->kind, toPin, false);
    if (link.fromPinId == 0U || link.toPinId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link could not resolve stable pin ids.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    link.id = MakeRenderMaterialGraphLinkId(link);
    if (FindRenderMaterialGraphLink(graph, link.id) != nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link id is duplicated.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    link.fromPin = std::move(fromPin);
    link.toPin = std::move(toPin);
    graph.links.push_back(std::move(link));
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLinkWithStableIds(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string linkIdText;
    std::string fromNodeText;
    std::string fromPinIdText;
    std::string fromPin;
    std::string toNodeText;
    std::string toPinIdText;
    std::string toPin;
    if (!(stream >> linkIdText >> fromNodeText >> fromPinIdText >> fromPin >> toNodeText >> toPinIdText >> toPin)) {
        return ParseGraphLink(rest, line, graph, diagnostics);
    }

    RenderMaterialGraphLink link{};
    if (!ParseUInt32(linkIdText, link.id) || link.id == 0U ||
        !ParseUInt32(fromNodeText, link.fromNodeId) || !ParseUInt32(fromPinIdText, link.fromPinId) ||
        !ParseUInt32(toNodeText, link.toNodeId) || !ParseUInt32(toPinIdText, link.toPinId) ||
        link.fromNodeId == 0U || link.toNodeId == 0U || link.fromPinId == 0U || link.toPinId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph stable link requires positive link, node and pin ids.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (FindRenderMaterialGraphLink(graph, link.id) != nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link id is duplicated.", linkIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
    const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
    if (fromNode == nullptr || toNode == nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link references an undeclared node.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!IsRenderMaterialGraphOutputPin(fromNode->kind, fromPin) || RenderMaterialGraphStablePinId(fromNode->kind, fromPin, true) != link.fromPinId) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid source pin id/name pair.", fromPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!IsRenderMaterialGraphInputPin(toNode->kind, toPin) || RenderMaterialGraphStablePinId(toNode->kind, toPin, false) != link.toPinId) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid target pin id/name pair.", toPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!AreRenderMaterialGraphPinsCompatible(fromNode->kind, fromPin, toNode->kind, toPin)) {
        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(fromNode->kind, fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(toNode->kind, toPin, false);
        AddDiagnostic(
            diagnostics,
            RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink,
            line,
            "graphLink",
            "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".",
            std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const std::uint32_t expectedLinkId = MakeRenderMaterialGraphLinkId(link);
    if (expectedLinkId != link.id) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link id does not match its stable endpoints.", linkIdText);
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
    if (keyword == "graphMaterialDomain") {
        return ParseGraphMaterialDomain(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphShadingModel") {
        return ParseGraphShadingModel(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphStorageModel") {
        return ParseGraphStorageModel(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphDiagnosticSchemaVersion") {
        return ParseGraphDiagnosticSchemaVersion(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphPersistCompileDiagnostics") {
        return ParseGraphPersistCompileDiagnostics(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphArtifactFailurePolicy") {
        return ParseGraphArtifactFailurePolicy(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLastGoodArtifactAssetId") {
        return ParseGraphLastGoodArtifactAssetId(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLastGoodArtifactHash") {
        return ParseGraphLastGoodArtifactHash(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphNode") {
        return ParseGraphNode(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphParameter") {
        return ParseGraphParameter(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLink") {
        return ParseGraphLinkWithStableIds(rest, line, asset.graph, diagnostics);
    }
    return RenderMaterialGraphFieldParseResult::Unknown;
}

} // namespace kb::render
