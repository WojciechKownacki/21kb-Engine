#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"

#include <fstream>
#include <memory>

namespace kb::render {
namespace {

[[nodiscard]] bool HasFunctionOutput(const RenderMaterialGraphDocument& graph) noexcept {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
            return true;
        }
    }
    return false;
}

void ValidateFunctionGraph(RenderMaterialAssetParseResult& result, kb::assets::AssetId assetId, const std::filesystem::path& path) {
    if (!result.asset.has_value()) {
        return;
    }
    if (!HasFunctionOutput(result.asset->graph)) {
        result.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode,
            .assetId = assetId,
            .path = path,
            .field = "FunctionOutput",
            .message = "Material function asset requires at least one FunctionOutput node.",
        });
        result.asset.reset();
    }
}

[[nodiscard]] RenderMaterialFunctionAssetData ToFunctionAsset(const RenderMaterialAssetData& carrier) {
    return RenderMaterialFunctionAssetData{
        .documentVersion = carrier.graph.documentVersion,
        .hasExplicitDocumentVersion = carrier.graph.hasExplicitDocumentVersion,
        .graph = carrier.graph,
    };
}

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    for (const kb::assets::AssetId existing : dependencies) {
        if (existing == id) {
            return;
        }
    }
    dependencies.push_back(id);
}

} // namespace

std::string_view RenderMaterialFunctionAssetLoader::Type() const noexcept {
    return kRenderMaterialFunctionAssetType;
}

std::type_index RenderMaterialFunctionAssetLoader::PayloadType() const noexcept {
    return typeid(RenderMaterialFunctionAssetData);
}

std::vector<std::string> RenderMaterialFunctionAssetLoader::Extensions() const {
    return { kRenderMaterialFunctionAssetExtension };
}

kb::assets::AssetLoadResult RenderMaterialFunctionAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    RenderMaterialAssetParseResult result = LoadFunctionWithDiagnostics(request.resolvedPath, request.metadata.id);
    if (!result.asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = result.ErrorMessage() };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderMaterialFunctionAssetData>(ToFunctionAsset(*result.asset)),
        .error = {},
    };
}

std::vector<kb::assets::AssetId> RenderMaterialFunctionAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const RenderMaterialAssetParseResult result = LoadFunctionWithDiagnostics(metadata.physicalPath, metadata.id);
    if (!result.asset.has_value()) {
        return {};
    }
    std::vector<kb::assets::AssetId> dependencies;
    if (result.asset->graph.lastGoodArtifact.assetId != 0U) {
        AppendUnique(dependencies, kb::assets::AssetId{ result.asset->graph.lastGoodArtifact.assetId });
    }
    for (const std::uint64_t functionAssetId : DiscoverRenderMaterialGraphFunctionDependencies(result.asset->graph)) {
        const kb::assets::AssetId id{ functionAssetId };
        const kb::assets::AssetMetadata* dependency = registry.Find(id);
        if (dependency != nullptr && dependency->type == kRenderMaterialFunctionAssetType) {
            AppendUnique(dependencies, id);
        }
    }
    return dependencies;
}

std::optional<RenderMaterialFunctionAssetData> RenderMaterialFunctionAssetLoader::LoadFunction(const std::filesystem::path& path) {
    RenderMaterialAssetParseResult result = LoadFunctionWithDiagnostics(path);
    return result.asset.has_value() ? std::optional<RenderMaterialFunctionAssetData>{ ToFunctionAsset(*result.asset) } : std::nullopt;
}

std::optional<RenderMaterialFunctionAssetData> RenderMaterialFunctionAssetLoader::LoadFunction(std::istream& input) {
    RenderMaterialAssetParseResult result = LoadFunctionWithDiagnostics(input);
    return result.asset.has_value() ? std::optional<RenderMaterialFunctionAssetData>{ ToFunctionAsset(*result.asset) } : std::nullopt;
}

RenderMaterialAssetParseResult RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(const std::filesystem::path& path) {
    return LoadFunctionWithDiagnostics(path, {});
}

RenderMaterialAssetParseResult RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(const std::filesystem::path& path, kb::assets::AssetId assetId) {
    RenderMaterialAssetParseResult result = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(path, assetId);
    ValidateFunctionGraph(result, assetId, path);
    return result;
}

RenderMaterialAssetParseResult RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(std::istream& input) {
    RenderMaterialAssetParseResult result = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(input);
    ValidateFunctionGraph(result, {}, {});
    return result;
}

bool RenderMaterialFunctionAssetLoader::SaveFunction(const std::filesystem::path& path, const RenderMaterialFunctionAssetData& function) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output) {
        return false;
    }
    output << "# KB material function\n";
    WriteRenderMaterialGraphDocument(output, function.graph);
    return static_cast<bool>(output);
}

} // namespace kb::render
