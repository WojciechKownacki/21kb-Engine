#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "RenderMaterialAtomicFileWriter.hpp"
#include "resources/RenderMaterialGraphFieldParser.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace kb::render {
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

[[nodiscard]] std::string_view StripComment(std::string_view text) noexcept {
    const std::size_t comment = text.find('#');
    return comment == std::string_view::npos ? text : text.substr(0U, comment);
}

void AddDiagnostic(
    RenderMaterialAssetParseResult& result,
    RenderMaterialAssetParseDiagnosticCode code,
    std::size_t line,
    std::string field,
    std::string message,
    std::string text = {}) {
    result.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
        .code = code,
        .line = line,
        .field = std::move(field),
        .message = std::move(message),
        .text = std::move(text),
    });
}

[[nodiscard]] bool HasError(const std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics) noexcept {
    return std::ranges::any_of(diagnostics, [](const RenderMaterialAssetParseDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Error;
    });
}

[[nodiscard]] RenderMaterialAssetParseResult ParseGraph(std::istream& input) {
    RenderMaterialAssetParseResult result{};
    RenderMaterialAssetData carrier{};
    carrier.graph = RenderMaterialGraphDocument{};
    bool sawContent = false;
    std::size_t graphShadingModelLine = 0U;
    std::string graphShadingModelSourceText;
    std::size_t graphLastGoodArtifactLine = 0U;

    std::string line;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }
        sawContent = true;

        const std::size_t split = trimmed.find_first_of(" \t");
        const std::string_view keyword = split == std::string_view::npos ? trimmed : trimmed.substr(0U, split);
        const std::string_view rest = split == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(split + 1U));
        if (keyword == "graphShadingModel") {
            graphShadingModelLine = lineNumber;
            graphShadingModelSourceText = std::string{ trimmed };
        } else if (keyword == "graphLastGoodArtifactAssetId" || keyword == "graphLastGoodArtifactHash") {
            graphLastGoodArtifactLine = lineNumber;
        }
        const RenderMaterialGraphFieldParseResult parsed =
            RenderMaterialGraphFieldParser::Apply(keyword, rest, lineNumber, carrier, result.diagnostics);
        if (parsed == RenderMaterialGraphFieldParseResult::Unknown) {
            AddDiagnostic(
                result,
                RenderMaterialAssetParseDiagnosticCode::UnknownField,
                lineNumber,
                std::string{ keyword },
                "Unknown material graph asset field.",
                std::string{ trimmed });
        }
    }

    if (!sawContent) {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::EmptyDocument, 0U, {}, "Material graph asset is empty.");
    }
    if (carrier.graph.nodes.empty()) {
        AddDiagnostic(result, RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode, 0U, "graphNode", "Material graph asset must contain at least one node.");
    }
    if (sawContent) {
        FinalizeRenderMaterialGraphDocument(
            carrier.graph,
            result.diagnostics,
            graphShadingModelLine,
            graphShadingModelSourceText,
            graphLastGoodArtifactLine);
    }
    // Migration and compatibility diagnostics are warnings. They must remain visible without
    // making an otherwise valid standalone graph unloadable.
    if (!HasError(result.diagnostics)) {
        result.asset = std::move(carrier);
    }
    return result;
}

} // namespace

std::string_view RenderMaterialGraphAssetLoader::Type() const noexcept {
    return kRenderMaterialGraphAssetType;
}

std::type_index RenderMaterialGraphAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialGraphDocument);
}

std::vector<std::string> RenderMaterialGraphAssetLoader::Extensions() const {
    return { kRenderMaterialGraphAssetExtension };
}

kb::assets::AssetLoadResult RenderMaterialGraphAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialAssetParseResult result = LoadGraphWithDiagnostics(request.resolvedPath, request.metadata.id);
    if (!result.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = result.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialGraphDocument>(result.asset->graph),
        .error = {},
    };
}

std::vector<kb::assets::AssetId> RenderMaterialGraphAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const RenderMaterialAssetParseResult result = LoadGraphWithDiagnostics(metadata.physicalPath, metadata.id);
    if (!result.asset.has_value()) {
        return {};
    }
    std::vector<kb::assets::AssetId> dependencies;
    const auto appendTyped = [&dependencies, &registry](std::uint64_t assetId, std::string_view expectedType) {
        if (assetId == 0U || std::ranges::find(dependencies, kb::assets::AssetId{ assetId }) != dependencies.end()) {
            return;
        }
        const kb::assets::AssetMetadata* dependency = registry.Find(kb::assets::AssetId{ assetId });
        if (dependency != nullptr && dependency->type == expectedType) {
            dependencies.push_back(dependency->id);
        }
    };
    if (result.asset->graph.lastGoodArtifact.assetId != 0U) {
        dependencies.push_back(kb::assets::AssetId{ result.asset->graph.lastGoodArtifact.assetId });
    }
    for (const std::uint64_t assetId : DiscoverRenderMaterialGraphFunctionDependencies(result.asset->graph)) {
        appendTyped(assetId, kRenderMaterialFunctionAssetType);
    }
    for (const std::uint64_t assetId : DiscoverRenderMaterialGraphParameterCollectionDependencies(result.asset->graph)) {
        appendTyped(assetId, kRenderMaterialParameterCollectionAssetType);
    }
    return dependencies;
}

std::optional<RenderMaterialGraphDocument> RenderMaterialGraphAssetLoader::LoadGraph(const std::filesystem::path& path) {
    RenderMaterialAssetParseResult result = LoadGraphWithDiagnostics(path);
    return result.asset.has_value() ? std::optional<RenderMaterialGraphDocument>{ result.asset->graph } : std::nullopt;
}

std::optional<RenderMaterialGraphDocument> RenderMaterialGraphAssetLoader::LoadGraph(std::istream& input) {
    RenderMaterialAssetParseResult result = LoadGraphWithDiagnostics(input);
    return result.asset.has_value() ? std::optional<RenderMaterialGraphDocument>{ result.asset->graph } : std::nullopt;
}

RenderMaterialAssetParseResult RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(const std::filesystem::path& path) {
    return LoadGraphWithDiagnostics(path, {});
}

RenderMaterialAssetParseResult RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        RenderMaterialAssetParseResult result{};
        result.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::FileOpenFailed,
            .assetId = assetId,
            .path = path,
            .message = "Material graph asset file could not be opened.",
        });
        return result;
    }

    RenderMaterialAssetParseResult result = ParseGraph(input);
    for (RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        diagnostic.assetId = assetId;
        diagnostic.path = path;
    }
    return result;
}

RenderMaterialAssetParseResult RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(std::istream& input) {
    return ParseGraph(input);
}

bool RenderMaterialGraphAssetLoader::SaveGraph(const std::filesystem::path& path, const RenderMaterialGraphDocument& graph) {
    return detail::WriteMaterialFileAtomically(path, [&graph](std::ostream& output) {
        output << "# KB material graph\n";
        WriteRenderMaterialGraphDocument(output, graph);
    });
}

} // namespace kb::render
