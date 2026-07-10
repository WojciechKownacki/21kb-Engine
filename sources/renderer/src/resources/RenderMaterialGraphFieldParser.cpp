#include "resources/RenderMaterialGraphFieldParser.hpp"

#include <charconv>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

void AddGraphMigrationDiagnostic(
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text) {
    diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
        .code = RenderMaterialAssetParseDiagnosticCode::GraphMigration,
        .severity = RenderMaterialAssetParseDiagnosticSeverity::Warning,
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
            if (code == "0A") {
                decoded += '\n';
                index += 2U;
                continue;
            }
            if (code == "0D") {
                decoded += '\r';
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

[[nodiscard]] bool ParseSamplerFilter(std::string_view text, RenderMaterialGraphSamplerFilter& output) noexcept {
    if (text == "Linear") {
        output = RenderMaterialGraphSamplerFilter::Linear;
        return true;
    }
    if (text == "Point") {
        output = RenderMaterialGraphSamplerFilter::Point;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseSamplerWrap(std::string_view text, RenderMaterialGraphSamplerWrap& output) noexcept {
    if (text == "Repeat") {
        output = RenderMaterialGraphSamplerWrap::Repeat;
        return true;
    }
    if (text == "Clamp") {
        output = RenderMaterialGraphSamplerWrap::Clamp;
        return true;
    }
    if (text == "Mirror") {
        output = RenderMaterialGraphSamplerWrap::Mirror;
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

[[nodiscard]] bool FindGraphComment(const RenderMaterialGraphDocument& graph, std::uint32_t commentId) noexcept {
    for (const RenderMaterialGraphCommentBox& comment : graph.comments) {
        if (comment.id == commentId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool FindGraphComposite(const RenderMaterialGraphDocument& graph, std::uint32_t compositeId) noexcept {
    for (const RenderMaterialGraphCompositeSubgraph& composite : graph.composites) {
        if (composite.id == compositeId) {
            return true;
        }
    }
    return false;
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
    if (version > kRenderMaterialGraphDocumentVersion) {
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
    // MAT-37: accept every declared shading model token. ParseRenderMaterialShadingModel recognises the
    // non-DefaultLit models unambiguously; DefaultLit is only accepted for its own spellings so unknown
    // garbage is still rejected rather than silently treated as DefaultLit.
    const RenderMaterialShadingModel model = ParseRenderMaterialShadingModel(rest);
    const bool recognisedDefaultLit = rest == "lit" || rest == "defaultLit" || rest == "default_lit" || rest == "defaultlit";
    if (model == RenderMaterialShadingModel::DefaultLit && !recognisedDefaultLit) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphShadingModel", "Unsupported material graph shading model.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.shadingModel = std::string{ rest };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphBlendMode(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    // MAT-38: accept every declared blend-mode token. ParseRenderMaterialGraphBlendMode recognises the
    // non-Opaque modes unambiguously; Opaque is only accepted for its own spelling so unknown tokens fail.
    const RenderMaterialGraphBlendMode mode = ParseRenderMaterialGraphBlendMode(rest);
    const bool recognisedOpaque = rest == "opaque";
    if (mode == RenderMaterialGraphBlendMode::Opaque && !recognisedOpaque) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphBlendMode", "Unsupported material graph blend mode.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    graph.blendMode = std::string{ rest };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphStorageModel(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    if (rest != "inline-kbmat" && rest != "material-graph-asset" && rest != "material-function-asset") {
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
            node->kind == RenderMaterialGraphNodeKind::TextureObject ||
            node->kind == RenderMaterialGraphNodeKind::TextureSampleCube ||
            node->kind == RenderMaterialGraphNodeKind::TextureObjectCube ||
            node->kind == RenderMaterialGraphNodeKind::TextureSampleVolume ||
            node->kind == RenderMaterialGraphNodeKind::TextureObjectVolume ||
            node->kind == RenderMaterialGraphNodeKind::TextureSample2DArray ||
            node->kind == RenderMaterialGraphNodeKind::TextureObject2DArray ||
            node->kind == RenderMaterialGraphNodeKind::ConstantScalar ||
            node->kind == RenderMaterialGraphNodeKind::ConstantBool ||
            node->kind == RenderMaterialGraphNodeKind::ConstantVector2 ||
            node->kind == RenderMaterialGraphNodeKind::ConstantVector ||
            node->kind == RenderMaterialGraphNodeKind::ConstantColor ||
            node->kind == RenderMaterialGraphNodeKind::CollectionParameter ||
            node->kind == RenderMaterialGraphNodeKind::ColorRamp ||
            node->kind == RenderMaterialGraphNodeKind::Uv ||
            node->kind == RenderMaterialGraphNodeKind::StaticBoolParameter ||
            node->kind == RenderMaterialGraphNodeKind::StaticSwitch ||
            node->kind == RenderMaterialGraphNodeKind::StaticComponentMask ||
            node->kind == RenderMaterialGraphNodeKind::Reroute ||
            node->kind == RenderMaterialGraphNodeKind::NamedRerouteDeclaration ||
            node->kind == RenderMaterialGraphNodeKind::NamedRerouteUsage ||
            node->kind == RenderMaterialGraphNodeKind::CompositeInput ||
            node->kind == RenderMaterialGraphNodeKind::CompositeOutput ||
            node->kind == RenderMaterialGraphNodeKind::FunctionInput ||
            node->kind == RenderMaterialGraphNodeKind::FunctionOutput ||
            node->kind == RenderMaterialGraphNodeKind::MaterialFunctionCall ||
            node->kind == RenderMaterialGraphNodeKind::LayerStack);
    if (!supportsMetadata) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphParameter", "Material graph parameter metadata must reference an existing parameter, constant, texture sample or graph organization node.", nodeIdText);
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

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphSamplerState(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string nodeIdText;
    std::string minFilterText;
    std::string magFilterText;
    std::string mipFilterText;
    std::string wrapUText;
    std::string wrapVText;
    std::string trailing;
    if (!(stream >> nodeIdText >> minFilterText >> magFilterText >> mipFilterText >> wrapUText >> wrapVText) ||
        (stream >> trailing)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line,
            "graphSamplerState", "Material graph sampler state requires node id, min/mag/mip filters and U/V wrap modes.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    std::uint32_t nodeId = 0U;
    RenderMaterialGraphNode* node = nullptr;
    RenderMaterialGraphSamplerState state{};
    if (!ParseUInt32(nodeIdText, nodeId) || nodeId == 0U ||
        (node = FindMutableGraphNode(graph, nodeId)) == nullptr ||
        !ParseSamplerFilter(minFilterText, state.minFilter) ||
        !ParseSamplerFilter(magFilterText, state.magFilter) ||
        !ParseSamplerFilter(mipFilterText, state.mipFilter) ||
        !ParseSamplerWrap(wrapUText, state.wrapU) ||
        !ParseSamplerWrap(wrapVText, state.wrapV)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line,
            "graphSamplerState", "Material graph sampler state references an invalid node or enum value.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    node->parameter.samplerState = state;
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] bool ParseCustomPinList(std::string_view text, std::vector<RenderMaterialGraphCustomPin>& pins) {
    pins.clear();
    if (text.empty() || text == "_") {
        return true;
    }
    std::stringstream stream{ std::string{ text } };
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::size_t separator = item.find(':');
        if (separator == std::string::npos || separator == 0U || separator + 1U >= item.size()) {
            return false;
        }
        const std::string name = DecodeToken(std::string_view{ item }.substr(0U, separator));
        const std::optional<RenderMaterialGraphPinType> type = ParseRenderMaterialGraphPinType(std::string_view{ item }.substr(separator + 1U));
        if (!type.has_value()) {
            return false;
        }
        pins.push_back(RenderMaterialGraphCustomPin{
            .name = name,
            .type = *type,
        });
    }
    return true;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphCustomCode(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string nodeIdText;
    std::string outputTypeText;
    std::string inputsText;
    std::string outputsText;
    std::string definesText;
    std::string includesText;
    std::string bodyText;
    if (!(stream >> nodeIdText >> outputTypeText >> inputsText >> outputsText >> definesText >> includesText >> bodyText)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphCustomCode", "Material graph custom-code/function-call node requires node id, output type, inputs, outputs, defines, includes and body.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    std::uint32_t nodeId = 0U;
    if (!ParseUInt32(nodeIdText, nodeId) || nodeId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphCustomCode", "Material graph custom-code node id must be a positive unsigned integer.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    RenderMaterialGraphNode* node = FindMutableGraphNode(graph, nodeId);
    if (node == nullptr ||
        (node->kind != RenderMaterialGraphNodeKind::CustomCode &&
         node->kind != RenderMaterialGraphNodeKind::MaterialFunctionCall)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphCustomCode", "Material graph custom-code metadata must reference an existing CustomCode or MaterialFunctionCall node.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    const std::optional<RenderMaterialGraphPinType> outputType = ParseRenderMaterialGraphPinType(outputTypeText);
    std::vector<RenderMaterialGraphCustomPin> inputs;
    std::vector<RenderMaterialGraphCustomPin> outputs;
    if (!outputType.has_value() ||
        !ParseCustomPinList(inputsText, inputs) ||
        !ParseCustomPinList(outputsText, outputs)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphCustomCode", "Material graph custom-code metadata has an invalid type or pin list.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    node->customCode = RenderMaterialGraphCustomCode{
        .body = bodyText == "_" ? std::string{} : DecodeToken(bodyText),
        .outputType = *outputType,
        .inputs = std::move(inputs),
        .outputs = std::move(outputs),
        .defines = definesText == "_" ? std::string{} : DecodeToken(definesText),
        .includes = includesText == "_" ? std::string{} : DecodeToken(includesText),
    };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLayerStackEntry(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string nodeIdText;
    std::string indexText;
    std::string layerFunctionText;
    std::string blendFunctionText;
    std::string enabledText;
    std::string layerNameText;
    std::string blendNameText;
    std::string linkStateText;
    if (!(stream >> nodeIdText >> indexText >> layerFunctionText >> blendFunctionText >> enabledText >>
            layerNameText >> blendNameText >> linkStateText)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackEntry", "Material graph layer stack entry requires node id, index, layer function asset id, blend function asset id, enabled flag, layer name, blend name and link state.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    std::uint32_t nodeId = 0U;
    std::uint32_t index = 0U;
    std::uint64_t layerFunctionAssetId = 0U;
    std::uint64_t blendFunctionAssetId = 0U;
    bool enabled = true;
    if (!ParseUInt32(nodeIdText, nodeId) || nodeId == 0U ||
        !ParseUInt32(indexText, index) ||
        !ParseUInt64(layerFunctionText, layerFunctionAssetId) ||
        !ParseUInt64(blendFunctionText, blendFunctionAssetId) ||
        !ParseBool(enabledText, enabled)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackEntry", "Material graph layer stack entry has an invalid id, index, asset id or enabled flag.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphNode* node = FindMutableGraphNode(graph, nodeId);
    if (node == nullptr || node->kind != RenderMaterialGraphNodeKind::LayerStack) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackEntry", "Material graph layer stack entry must reference an existing LayerStack node.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    if (node->layerStack.size() <= index) {
        node->layerStack.resize(static_cast<std::size_t>(index) + 1U);
    }
    std::vector<RenderMaterialGraphLayerStackParameter> layerParameters =
        std::move(node->layerStack[index].layerParameters);
    std::vector<RenderMaterialGraphLayerStackParameter> blendParameters =
        std::move(node->layerStack[index].blendParameters);
    node->layerStack[index] = RenderMaterialGraphLayerStackEntry{
        .layerFunctionAssetId = layerFunctionAssetId,
        .blendFunctionAssetId = blendFunctionAssetId,
        .enabled = enabled,
        .layerName = layerNameText == "_" ? std::string{} : DecodeToken(layerNameText),
        .blendName = blendNameText == "_" ? std::string{} : DecodeToken(blendNameText),
        .linkState = linkStateText == "_" ? std::string{} : DecodeToken(linkStateText),
        .layerParameters = std::move(layerParameters),
        .blendParameters = std::move(blendParameters),
    };
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphLayerStackParameter(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string nodeIdText;
    std::string indexText;
    std::string ownerText;
    std::string pinNameText;
    std::string typeText;
    std::string valueText;
    if (!(stream >> nodeIdText >> indexText >> ownerText >> pinNameText >> typeText >> valueText)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackParameter", "Material graph layer stack parameter requires node id, index, owner, pin name, pin type and value.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    std::uint32_t nodeId = 0U;
    std::uint32_t index = 0U;
    const std::optional<RenderMaterialGraphPinType> type = ParseRenderMaterialGraphPinType(typeText);
    if (!ParseUInt32(nodeIdText, nodeId) || nodeId == 0U ||
        !ParseUInt32(indexText, index) ||
        !type.has_value() ||
        pinNameText == "_") {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackParameter", "Material graph layer stack parameter has an invalid id, index, pin name or type.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphNode* node = FindMutableGraphNode(graph, nodeId);
    if (node == nullptr || node->kind != RenderMaterialGraphNodeKind::LayerStack) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackParameter", "Material graph layer stack parameter must reference an existing LayerStack node.", nodeIdText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (node->layerStack.size() <= index) {
        node->layerStack.resize(static_cast<std::size_t>(index) + 1U);
    }

    RenderMaterialGraphLayerStackParameter parameter{
        .pinName = DecodeToken(pinNameText),
        .type = *type,
        .valueHint = valueText == "_" ? std::string{} : DecodeToken(valueText),
    };
    if (ownerText == "layer") {
        node->layerStack[index].layerParameters.push_back(std::move(parameter));
        return RenderMaterialGraphFieldParseResult::Parsed;
    }
    if (ownerText == "blend") {
        node->layerStack[index].blendParameters.push_back(std::move(parameter));
        return RenderMaterialGraphFieldParseResult::Parsed;
    }

    AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, line, "graphLayerStackParameter", "Material graph layer stack parameter owner must be 'layer' or 'blend'.", ownerText);
    return RenderMaterialGraphFieldParseResult::Failed;
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
    if (!IsRenderMaterialGraphOutputPin(*fromNode, fromPin)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid source pin.", fromPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!IsRenderMaterialGraphInputPin(*toNode, toPin)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid target pin.", toPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!AreRenderMaterialGraphPinsCompatible(*fromNode, fromPin, *toNode, toPin)) {
        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(*fromNode, fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(*toNode, toPin, false);
        AddDiagnostic(
            diagnostics,
            RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink,
            line,
            "graphLink",
            "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".",
            std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    link.fromPinId = RenderMaterialGraphStablePinId(*fromNode, fromPin, true);
    link.toPinId = RenderMaterialGraphStablePinId(*toNode, toPin, false);
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
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId);
    const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId);
    if (fromNode == nullptr || toNode == nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link references an undeclared node.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const std::uint32_t expectedFromPinId = RenderMaterialGraphStablePinId(*fromNode, fromPin, true);
    if (!IsRenderMaterialGraphOutputPin(*fromNode, fromPin) || expectedFromPinId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid source pin.", fromPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const std::uint32_t expectedToPinId = RenderMaterialGraphStablePinId(*toNode, toPin, false);
    if (!IsRenderMaterialGraphInputPin(*toNode, toPin) || expectedToPinId == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link uses an invalid target pin.", toPin);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!AreRenderMaterialGraphPinsCompatible(*fromNode, fromPin, *toNode, toPin)) {
        const RenderMaterialGraphPinType fromType = RenderMaterialGraphPinDataType(*fromNode, fromPin, true);
        const RenderMaterialGraphPinType toType = RenderMaterialGraphPinDataType(*toNode, toPin, false);
        AddDiagnostic(
            diagnostics,
            RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink,
            line,
            "graphLink",
            "Material graph link type mismatch: " + std::string{ RenderMaterialGraphPinTypeName(fromType) } + " cannot connect to " + std::string{ RenderMaterialGraphPinTypeName(toType) } + ".",
            std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    const bool migratedStableIds = link.fromPinId != expectedFromPinId || link.toPinId != expectedToPinId;
    link.fromPinId = expectedFromPinId;
    link.toPinId = expectedToPinId;
    const std::uint32_t expectedLinkId = MakeRenderMaterialGraphLinkId(link);
    const bool migratedLinkId = expectedLinkId != link.id;
    link.id = expectedLinkId;
    if (FindRenderMaterialGraphLink(graph, link.id) != nullptr) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, line, "graphLink", "Material graph link id is duplicated.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (migratedStableIds || migratedLinkId) {
        AddGraphMigrationDiagnostic(
            diagnostics,
            line,
            "graphLink",
            "Material graph link stable ids were migrated to the current schema.",
            std::string{ rest });
    }

    link.fromPin = std::move(fromPin);
    link.toPin = std::move(toPin);
    graph.links.push_back(std::move(link));
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphComment(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string idText;
    std::string xText;
    std::string yText;
    std::string widthText;
    std::string heightText;
    std::string colorText;
    std::string text;
    if (!(stream >> idText >> xText >> yText >> widthText >> heightText >> colorText >> text)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComment", "Material graph comment requires id, x, y, width, height, color and text.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphCommentBox comment{};
    if (!ParseUInt32(idText, comment.id) || comment.id == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComment", "Material graph comment id must be a positive unsigned integer.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (FindGraphComment(graph, comment.id)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComment", "Material graph comment id is duplicated.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseInt32(xText, comment.positionX) || !ParseInt32(yText, comment.positionY) ||
        !ParseInt32(widthText, comment.width) || !ParseInt32(heightText, comment.height) ||
        comment.width <= 0 || comment.height <= 0) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComment", "Material graph comment bounds must use signed coordinates and positive size.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseUInt32(colorText, comment.color)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComment", "Material graph comment color must be an unsigned integer.", colorText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    comment.text = text == "_" ? std::string{} : DecodeToken(text);
    graph.comments.push_back(std::move(comment));
    return RenderMaterialGraphFieldParseResult::Parsed;
}

[[nodiscard]] bool ParseGraphCompositeNodeIds(
    std::string_view text,
    const RenderMaterialGraphDocument& graph,
    std::vector<std::uint32_t>& nodeIds) {
    nodeIds.clear();
    if (text.empty() || text == "_") {
        return true;
    }
    std::stringstream stream{ std::string{ text } };
    std::string item;
    while (std::getline(stream, item, ',')) {
        std::uint32_t nodeId = 0U;
        if (!ParseUInt32(item, nodeId) || nodeId == 0U ||
            FindRenderMaterialGraphNode(graph, nodeId) == nullptr) {
            return false;
        }
        nodeIds.push_back(nodeId);
    }
    return true;
}

[[nodiscard]] RenderMaterialGraphFieldParseResult ParseGraphComposite(
    std::string_view rest,
    std::size_t line,
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) {
    std::istringstream stream{ std::string{ rest } };
    std::string idText;
    std::string xText;
    std::string yText;
    std::string widthText;
    std::string heightText;
    std::string colorText;
    std::string collapsedText;
    std::string nameText;
    std::string nodeIdsText;
    if (!(stream >> idText >> xText >> yText >> widthText >> heightText >> colorText >> collapsedText >> nameText >> nodeIdsText)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite requires id, x, y, width, height, color, collapsed, name and node ids.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }

    RenderMaterialGraphCompositeSubgraph composite{};
    if (!ParseUInt32(idText, composite.id) || composite.id == 0U) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite id must be a positive unsigned integer.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (FindGraphComposite(graph, composite.id)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite id is duplicated.", idText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseInt32(xText, composite.positionX) || !ParseInt32(yText, composite.positionY) ||
        !ParseInt32(widthText, composite.width) || !ParseInt32(heightText, composite.height) ||
        composite.width <= 0 || composite.height <= 0) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite bounds must use signed coordinates and positive size.", std::string{ rest });
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseUInt32(colorText, composite.color)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite color must be an unsigned integer.", colorText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseBool(collapsedText, composite.collapsed)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite collapsed flag must be true or false.", collapsedText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    if (!ParseGraphCompositeNodeIds(nodeIdsText, graph, composite.nodeIds)) {
        AddDiagnostic(diagnostics, RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, line, "graphComposite", "Material graph composite node id list references an undeclared node.", nodeIdsText);
        return RenderMaterialGraphFieldParseResult::Failed;
    }
    composite.name = nameText == "_" ? std::string{} : DecodeToken(nameText);
    graph.composites.push_back(std::move(composite));
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
    if (keyword == "graphBlendMode") {
        return ParseGraphBlendMode(rest, line, asset.graph, diagnostics);
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
    if (keyword == "graphSamplerState") {
        return ParseGraphSamplerState(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphCustomCode") {
        return ParseGraphCustomCode(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLayerStackEntry") {
        return ParseGraphLayerStackEntry(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLayerStackParameter") {
        return ParseGraphLayerStackParameter(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphLink") {
        return ParseGraphLinkWithStableIds(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphComment") {
        return ParseGraphComment(rest, line, asset.graph, diagnostics);
    }
    if (keyword == "graphComposite") {
        return ParseGraphComposite(rest, line, asset.graph, diagnostics);
    }
    return RenderMaterialGraphFieldParseResult::Unknown;
}

} // namespace kb::render
